/* 
 * <Copyright>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.  THIS SOFTWARE IS PROVIDED BY
 * THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO
 * EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * jerasure_rs_vand backend implementation
 *
 * vi: set noai tw=79 ts=4 sw=4:
 */

#include <stdio.h>
#include <stdlib.h>

#include "erasurecode.h"
#include "erasurecode_backend.h"
#include "erasurecode_helpers.h"

/* Forward declarations */
struct ec_backend_op_stubs jerasure_rs_vand_ops;
struct ec_backend jerasure_rs_vand;

struct jerasure_rs_vand_descriptor {
    /* calls required for init */
    int * (*reed_sol_vandermonde_coding_matrix)(int, int, int);
 
    /* calls required for encode */
    void (*jerasure_matrix_encode)(int, int, int, int*, char **, char **, int); 
    
    /* calls required for decode */
    int (*jerasure_matrix_decode)(int, int, int, int *, int, int*, char **, char **, int);
    
    /* calls required for reconstruct */
    int (*jerasure_make_decoding_matrix)(int, int, int, int *, int *, int *, int *);
    int * (*jerasure_erasures_to_erased)(int, int, int *);
    void (*jerasure_matrix_dotprod)(int, int, int *,int *, int,char **, char **, int);

    /* fields needed to hold state */
    int *matrix;
    int k;
    int m;
    int w;
};

static int jerasure_rs_vand_encode(void *desc, char **data, char **parity,
        int blocksize)
{
    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*) desc;

    /* FIXME - make jerasure_matrix_encode return a value */
    jerasure_desc->jerasure_matrix_encode(jerasure_desc->k, jerasure_desc->m,
            jerasure_desc->w, jerasure_desc->matrix, data, parity, blocksize);

    return 0;
}

static int jerasure_rs_vand_decode(void *desc, char **data, char **parity,
        int *missing_idxs, int blocksize)
{
    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*)desc;

    /* FIXME - make jerasure_matrix_decode return a value */
    jerasure_desc->jerasure_matrix_decode(jerasure_desc->k,
            jerasure_desc->m, jerasure_desc->w,
            jerasure_desc->matrix, 1, missing_idxs, data, parity, blocksize);
}

static int jerasure_rs_vand_reconstruct(void *desc, char **data, char **parity,
        int *missing_idxs, int destination_idx, int blocksize)
{
    int ret = 1;
    int *decoding_row;
    int *erased;

    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*) desc;
    int *dm_ids = (int *)
        alloc_zeroed_buffer(sizeof(int) * jerasure_desc->k);
    int *decoding_matrix = (int *)
        alloc_zeroed_buffer(sizeof(int*) * jerasure_desc->k * jerasure_desc->k);

    if (NULL == decoding_matrix || NULL == dm_ids) {
        goto out;
    }

    if (destination_idx < jerasure_desc->k) {

        erased = jerasure_desc->jerasure_erasures_to_erased(jerasure_desc->k,
                jerasure_desc->m, missing_idxs);

        ret = jerasure_desc->jerasure_make_decoding_matrix(jerasure_desc->k,
                jerasure_desc->m, jerasure_desc->w, jerasure_desc->matrix,
                erased, decoding_matrix, dm_ids);

        decoding_row = decoding_matrix + (destination_idx * jerasure_desc->k);

    } else {
        ret = 0;
        decoding_row = jerasure_desc->matrix +
            ((destination_idx - jerasure_desc->k) * jerasure_desc->k);
    }
      
    if (ret == 0) {
        jerasure_desc->jerasure_matrix_dotprod(jerasure_desc->k,
                jerasure_desc->w, decoding_row, dm_ids, destination_idx,
                data, parity, blocksize);
    }

out:
    if (NULL != decoding_matrix) {
        free(decoding_matrix);
    }
    if (NULL != dm_ids) {
        free(dm_ids);
    }

}

static int jerasure_rs_vand_min_fragments(void *desc, int *missing_idxs,
        int *fragments_needed)
{
    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*)desc;
    //ToDo (KMG): We need to move over the bitmap helper functions before we 
    // can implement this.
}

#define DEFAULT_W 16
static void * jerasure_rs_vand_init(struct ec_backend_args *args,
        void *backend_sohandle)
{
    struct jerasure_rs_vand_descriptor *desc =
        (struct jerasure_rs_vand_descriptor *) malloc(
                sizeof(struct jerasure_rs_vand_descriptor));

    if (NULL == desc) {
        return NULL;
    }

    desc->k = args->uargs.k;
    desc->m = args->uargs.m;

    if (args->uargs.w <= 0)
        args->uargs.w = DEFAULT_W;

    /* store w back in args so upper layer can get to it */
    desc->w = args->uargs.w;

    /* validate EC arguments */
    {
        long long max_symbols;
        if (desc->w != 8 && desc->w != 16 && desc->w != 32) {
            goto error;
        }
        max_symbols = 1LL << desc->w;
        if ((desc->k + desc->m) > max_symbols) {
            goto error;
        }
     }

    /* fill in function addresses */
    desc->jerasure_matrix_encode = dlsym(
            backend_sohandle, "jerasure_matrix_encode");
    if (NULL == desc->jerasure_matrix_encode) {
        goto error; 
    }
  
    desc->jerasure_matrix_decode = dlsym(
            backend_sohandle, "jerasure_matrix_decode");
    if (NULL == desc->jerasure_matrix_decode) {
        goto error; 
    }
  
    desc->jerasure_make_decoding_matrix = dlsym(
            backend_sohandle, "jerasure_make_decoding_matrix");
    if (NULL == desc->jerasure_make_decoding_matrix) {
        goto error; 
    }
  
    desc->jerasure_matrix_dotprod = dlsym(
            backend_sohandle, "jerasure_matrix_dotprod");
    if (NULL == desc->jerasure_matrix_dotprod) {
        goto error; 
    }
  
    desc->jerasure_erasures_to_erased = dlsym(
            backend_sohandle, "jerasure_erasures_to_erased");
    if (NULL == desc->jerasure_erasures_to_erased) {
        goto error; 
    }
 
    desc->reed_sol_vandermonde_coding_matrix = dlsym(
            backend_sohandle, "reed_sol_vandermonde_coding_matrix");
    if (NULL == desc->reed_sol_vandermonde_coding_matrix) {
        goto error; 
    }

    desc->matrix = desc->reed_sol_vandermonde_coding_matrix(
            desc->k, desc->m, desc->w);
    if (NULL == desc->matrix) {
        goto error; 
    }

    return desc;

error:
    if (NULL != desc) {
        free(desc);
    }
    return NULL;
}

/**
 * Return the element-size, which is the number of bits stored 
 * on a given device, per codeword.  For Vandermonde, this is 
 * 'w'.  For somthing like cauchy, this is packetsize * w. 
 * 
 * Returns the size in bits!
 */
static int
jerasure_rs_vand_element_size(void* desc)
{
    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*)desc;

    /* Note that cauchy will return pyeclib_handle->w * PYECC_CAUCHY_PACKETSIZE * 8 */
    return jerasure_desc->w;
}

static int jerasure_rs_vand_exit(void *desc)
{
    struct jerasure_rs_vand_descriptor *jerasure_desc = 
        (struct jerasure_rs_vand_descriptor*)desc;

    if (NULL != desc) {
        free(desc);
    }
}

struct ec_backend_op_stubs jerasure_rs_vand_op_stubs = {
    .INIT                       = jerasure_rs_vand_init,
    .EXIT                       = jerasure_rs_vand_exit,
    .ENCODE                     = jerasure_rs_vand_encode,
    .DECODE                     = jerasure_rs_vand_decode,
    .FRAGSNEEDED                = jerasure_rs_vand_min_fragments,
    .RECONSTRUCT                = jerasure_rs_vand_reconstruct,
    .ELEMENTSIZE                = jerasure_rs_vand_element_size,
};

struct ec_backend_common backend_jerasure_rs_vand = {
    .id                         = EC_BACKEND_JERASURE_RS_VAND,
    .name                       = "jerasure_rs_vand",
    .soname                     = "libJerasure.so",
    .soversion                  = "2.0",
    .ops                        = &jerasure_rs_vand_op_stubs,
};
