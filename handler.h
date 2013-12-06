/*****************************************************************************
Copyright 2013 Laboratory for Advanced Computing at the University of Chicago

This file is part of ucp

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

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <getopt.h>
#include <iostream>
#include <unistd.h>
#include <sys/stat.h>
#include "files.h"

#define BUFFER_LEN 67108864

typedef enum{
    XFER_DATA,
    XFER_FILENAME,
    XFER_DIRNAME,
    XFER_COMPLTE
} xfer_t;

typedef enum{
    MODE_SEND,
    MODE_RCV
} xfer_mode_t;


typedef struct header{
    int data_len;
    xfer_t type;
} header_t;

