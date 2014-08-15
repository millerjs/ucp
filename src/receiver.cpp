/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

	      This file is part of parcel by Joshua Miller

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing,
software distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions
and limitations under the License.
*****************************************************************************/

#include "util.h"
#include "parcel.h"
#include "files.h"
#include "receiver.h"
#include "postmaster.h"

// main loop for receiving mode, listens for headers and sorts out
// stream into files

postmaster_t*    receive_postmaster;
global_data_t    receive_data;


int read_header(header_t *header) 
{

    // return read(fileno(stdin), header, sizeof(header_t));
    return read(opts.recv_pipe[0], header, sizeof(header_t));

}

// wrapper for read
off_t read_data(void* b, int len) 
{

    off_t rs, total = 0;
    char* buffer = (char*)b;
    
    while (total < len) {
        // rs = read(fileno(stdin), buffer+total, len - total);
        rs = read(opts.recv_pipe[0], buffer+total, len - total);
        total += rs;
        TOTAL_XFER += rs;
    }

    verb(VERB_4, "Read %d bytes from stream", total);

    return total;

}

int receive_files(char*base_path) 
{
    header_t header;
    
    while (!opts.socket_ready) {
        usleep(10000);
    }

    int alloc_len = BUFFER_LEN - sizeof(header_t);
    receive_data.data = (char*) malloc( alloc_len * sizeof(char));

    // generate a base path for all destination files and get the
    // length
    receive_data.bl = generate_base_path(base_path, receive_data.data_path);
    
    // Read in headers and data until signalled completion
    while ( !receive_data.complete ) {

        if (receive_data.read_new_header) {
            if ((receive_data.rs = read_header(&header)) <= 0) {
                ERR("Bad header read");
            }
        }
        
        if (receive_data.rs) {
//            verb(VERB_2, "Dispatching message: %d", header.type);
            dispatch_message(receive_postmaster, header, &receive_data);
        }
    }
    

    return 0;
}

// ###########################################################
//
// header callbacks and postmaster init below
//
// ###########################################################

//
// pst_callback_dirname
//
// routine to handle XFER_DIRNAME message

int pst_callback_dirname(header_t header, global_data_t* global_data)
{
    
    verb(VERB_4, "Received directory header");
    
    // Read directory name from stream
    read_data(global_data->data_path + global_data->bl, header.data_len);

    if (opts.verbosity > VERB_1) {
        fprintf(stderr, "making directory: %s\n", global_data->data_path);
    }
    
    // make directory, if any parent in directory path
    // doesnt exist, make that as well
    mkdir_parent(global_data->data_path);

    // safety reset, data block after this will fault, expect a header
    global_data->expecting_data = 0;
    global_data->read_new_header = 1;
    
    return 0;
}

//
// pst_callback_filename
//
// routine to handle XFER_FILENAME message

int pst_callback_filename(header_t header, global_data_t* global_data)
{
    
    verb(VERB_4, "Received file header");
    
    // int f_mode = O_CREAT| O_WRONLY;
    int f_mode = O_CREAT| O_RDWR;
    int f_perm = 0666;

    // hang on to mtime data until we're done
    global_data->mtime_sec = header.mtime_sec;
    global_data->mtime_nsec = header.mtime_nsec;
    verb(VERB_2, "Header mtime: %d, mtime_nsec: %ld\n", global_data->mtime_sec, global_data->mtime_nsec);
    
    // Read filename from stream
    read_data(global_data->data_path + global_data->bl, header.data_len);

    verb(VERB_2, "Initializing file receive: %s\n", global_data->data_path + global_data->bl);


    global_data->fout = open(global_data->data_path, f_mode, f_perm);

    if (global_data->fout < 0) {

        // If we can't open the file, try building a
        // directory tree to it
        
        // Try and get a parent directory from file
        char parent_dir[MAX_PATH_LEN];
        get_parent_dir(parent_dir, global_data->data_path);
        
        if (opts.verbosity > VERB_2) {
            fprintf(stderr, "Using %s as parent directory.\n", parent_dir);
        }
        
        // Build parent directory recursively
        if (mkdir_parent(parent_dir) < 0) {
            perror("ERROR: recursive directory build failed");
        }

    }

    // If we had to build the directory path then retry file open
    if (global_data->fout < 0) {
        global_data->fout = open(global_data->data_path, f_mode, 0666);
    }

    if (global_data->fout < 0) {
        fprintf(stderr, "ERROR: %s ", global_data->data_path);
        perror("file open");
        clean_exit(EXIT_FAILURE);
    }

    // Attempt to optimize simple sequential write
    if (posix_fadvise64(global_data->fout, 0, 0, POSIX_FADV_SEQUENTIAL | POSIX_FADV_NOREUSE)) {
        if (opts.verbosity > VERB_3) {
            perror("WARNING: Unable to advise file write");
        }
    }		

    global_data->read_new_header = 1;
    global_data->expecting_data = 1;
    global_data->total = 0;

    return 0;

}

//
// pst_callback_f_size
//
// routine to handle XFER_F_SIZE message

int pst_callback_f_size(header_t header, global_data_t* global_data)
{

    // read in the size of the file
    read_data(&(global_data->f_size), header.data_len);

    // Memory map attempt
    if (opts.mmap) {
        map_fd(global_data->fout, global_data->f_size);
    }

    return 0;

}


//
// pst_callback_complete
//
// routine to handle XFER_COMPLETE message

int pst_callback_complete(header_t header, global_data_t* global_data)
{
    if (opts.verbosity > VERB_1) {
        fprintf(stderr, "Receive completed.\n");
    }
    
    global_data->complete = 1;
    
    return 0;
}


//
// pst_callback_data
//
// routine to handle XFER_DATA message

int pst_callback_data(header_t header, global_data_t* global_data)
{
    off_t rs, len;
    
    if (!global_data->expecting_data) {
        fprintf(stderr, "ERROR: Out of order data block.\n");
        clean_exit(EXIT_FAILURE);
    }

    // Either look to receive a whole buffer of
    // however much remains in the data block
    len = (BUFFER_LEN < (global_data->f_size - global_data->total)) ? BUFFER_LEN : (global_data->f_size - global_data->total);

    // read data buffer from stdin
    // use the memory map
    if (opts.mmap) {
        if ((rs = read_data(global_data->f_map + global_data->total, len)) < 0) {
            ERR("Unable to read stdin");
        }

    } else {
        if ((rs = read_data(global_data->data, len)) < 0) {
            ERR("Unable to read stdin");
        }

        // Write to file
        if ((write(global_data->fout, global_data->data, rs) < 0)) {
            perror("ERROR: unable to write to file");
            clean_exit(EXIT_FAILURE);
        }
    }

    global_data->total += rs;

//    read_header(&header);

    // Update user on progress if opts.progress set to true		    
    if (opts.progress) {
        print_progress(global_data->data_path, global_data->total, global_data->f_size);
    }


    return 0;
    
}

//
// pst_callback_data_complete
//
// routine to handle XFER_DATA_COMPLETE message

int pst_callback_data_complete(header_t header, global_data_t* global_data)
{
    // On the next loop, use the header that was just read in
   
    // Formatting
    if (opts.progress) {
        fprintf(stderr, "\n");
    }

    // Check to see if we received full file
    if (global_data->f_size) {
        if (global_data->total == global_data->f_size) {
            verb(VERB_3, "Received full file [%li B]", global_data->total);
        } else {
            warn("Did not receive full file: %s", global_data->data_path);
        }

    } else {
        warn("Completed stream of known size");
    }

    if (ftruncate64(global_data->fout, global_data->f_size)) {
        ERR("unable to truncate file to correct size");
    }
    
//    global_data->read_new_header = 0;
    global_data->expecting_data = 0;
    global_data->f_size = 0;
    
    // Truncate the file in case it already exists and remove extra data
    if (opts.mmap) {
        unmap_fd(global_data->fout, global_data->f_size);
    }

    close(global_data->fout);
    
    // fly - now is the time when we set the timestamps
    set_mod_time(global_data->data_path, global_data->mtime_nsec, global_data->mtime_sec);
    
    return 0;
}

void init_receiver()
{
    
    // initialize the data
    receive_data.f_size = 0;
    receive_data.complete = 0;
    receive_data.expecting_data = 0;
    receive_data.read_new_header = 1;
    
    // create the postmaster
    receive_postmaster = create_postmaster();

    // register the callbacks
    register_callback(receive_postmaster, XFER_DIRNAME, pst_callback_dirname);
    register_callback(receive_postmaster, XFER_FILENAME, pst_callback_filename);
    register_callback(receive_postmaster, XFER_F_SIZE, pst_callback_f_size);
    register_callback(receive_postmaster, XFER_COMPLETE, pst_callback_complete);
    register_callback(receive_postmaster, XFER_DATA, pst_callback_data);
    register_callback(receive_postmaster, XFER_DATA_COMPLETE, pst_callback_data_complete);
    
}