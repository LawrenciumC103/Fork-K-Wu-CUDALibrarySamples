/*
 * Copyright 1993-2021 NVIDIA Corporation.  All rights reserved.
 *
 * NOTICE TO LICENSEE:
 *
 * This source code and/or documentation ("Licensed Deliverables") are
 * subject to NVIDIA intellectual property rights under U.S. and
 * international Copyright laws.
 *
 * These Licensed Deliverables contained herein is PROPRIETARY and
 * CONFIDENTIAL to NVIDIA and is being provided under the terms and
 * conditions of a form of NVIDIA software license agreement by and
 * between NVIDIA and Licensee ("License Agreement") or electronically
 * accepted by Licensee.  Notwithstanding any terms or conditions to
 * the contrary in the License Agreement, reproduction or disclosure
 * of the Licensed Deliverables to any third party without the express
 * written consent of NVIDIA is prohibited.
 *
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE
 * LICENSE AGREEMENT, NVIDIA MAKES NO REPRESENTATION ABOUT THE
 * SUITABILITY OF THESE LICENSED DELIVERABLES FOR ANY PURPOSE.  IT IS
 * PROVIDED "AS IS" WITHOUT EXPRESS OR IMPLIED WARRANTY OF ANY KIND.
 * NVIDIA DISCLAIMS ALL WARRANTIES WITH REGARD TO THESE LICENSED
 * DELIVERABLES, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY,
 * NONINFRINGEMENT, AND FITNESS FOR A PARTICULAR PURPOSE.
 * NOTWITHSTANDING ANY TERMS OR CONDITIONS TO THE CONTRARY IN THE
 * LICENSE AGREEMENT, IN NO EVENT SHALL NVIDIA BE LIABLE FOR ANY
 * SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL DAMAGES, OR ANY
 * DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS,
 * WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS
 * ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THESE LICENSED DELIVERABLES.
 *
 * U.S. Government End Users.  These Licensed Deliverables are a
 * "commercial item" as that term is defined at 48 C.F.R. 2.101 (OCT
 * 1995), consisting of "commercial computer software" and "commercial
 * computer software documentation" as such terms are used in 48
 * C.F.R. 12.212 (SEPT 1995) and is provided to the U.S. Government
 * only as a commercial end item.  Consistent with 48 C.F.R.12.212 and
 * 48 C.F.R. 227.7202-1 through 227.7202-4 (JUNE 1995), all
 * U.S. Government End Users acquire the Licensed Deliverables with
 * only those rights set forth herein.
 *
 * Any use of the Licensed Deliverables in individual and commercial
 * software must include, in the user documentation and internal
 * comments to the code, the above Disclaimer and U.S. Government End
 * Users Notice.
 */
#pragma once
#include <cuda_runtime_api.h>  // cudaMalloc, cudaMemcpy, etc.
#include <cusp/csr_matrix.h>   // cusp::csr_matrix
#include <cusp/io/matrix_market.h>
#include <cusparse.h>  // cusparseSpMM
#include <stdio.h>     // printf
#include <stdlib.h>    // EXIT_FAILURE
#include <utils/generate_random_data.h>
// renamed this source file to .cpp to allow cstddef. Source:
// https://talk.pokitto.com/t/sudden-error-cstddef-no-such-file-or-directory/711/4
// renamed to .cu to allow cusp::csr_matrix<.,.,cusp::device_memory> instants as
// elaborated here:
// https://talk.pokitto.com/t/sudden-error-cstddef-no-such-file-or-directory/711/4
#include <utils/helper_string.h>

#include <chrono>

#include "npy.hpp"

#define CHECK_CUDA(func)                                                   \
  {                                                                        \
    cudaError_t status = (func);                                           \
    if (status != cudaSuccess) {                                           \
      printf("CUDA API failed at line %d with error: %s (%d)\n", __LINE__, \
             cudaGetErrorString(status), status);                          \
      return EXIT_FAILURE;                                                 \
    }                                                                      \
  }

#define CHECK_CUSPARSE(func)                                                   \
  {                                                                            \
    cusparseStatus_t status = (func);                                          \
    if (status != CUSPARSE_STATUS_SUCCESS) {                                   \
      printf("CUSPARSE API failed at line %d with error: %s (%d)\n", __LINE__, \
             cusparseGetErrorString(status), status);                          \
      return EXIT_FAILURE;                                                     \
    }                                                                          \
  }

int main_bench_spmm_csr(const int argc, const char **argv) {
  // Host problem definition
  int A_num_rows = getCmdLineArgumentInt(argc, argv, "A_num_rows");
  int A_num_cols = getCmdLineArgumentInt(argc, argv, "A_num_cols");
  int B_num_cols = getCmdLineArgumentInt(argc, argv, "B_num_cols");
  float A_sparsity = getCmdLineArgumentFloat(argc, argv, "A_sparsity");
  bool enable_dump = checkCmdLineFlag(argc, argv, "enable_dump");
  char *cli_result_path_and_prefix;
  bool flag_specify_result_path_and_prefix = getCmdLineArgumentString(
      argc, argv, "result_path_and_prefix", &cli_result_path_and_prefix);
  if (A_num_rows == 0 || A_num_cols == 0 || B_num_cols == 0 ||
      A_sparsity == 0.0f) {
    printf(
        "Usage: %s --A_num_rows=## --A_num_cols=## --B_num_cols=## "
        "--A_sparsity=0.## [--enable_dump] [--result_path_and_prefix=...]\n",
        argv[0]);
    return EXIT_FAILURE;
  }
  printf("A_num_rows: %d\n", A_num_rows);
  printf("A_num_cols: %d\n", A_num_cols);
  printf("B_num_cols: %d\n", B_num_cols);
  printf("A_sparsity: %f\n", A_sparsity);

  // ***** END OF HOST PROBLEM DEFINITION *****
  // int   A_nnz           = 9;
  int A_nnz = A_num_rows * A_num_cols * A_sparsity;
  int B_num_rows = A_num_cols;
  int ldb = B_num_rows;
  int ldc = A_num_rows;
  int B_size = ldb * B_num_cols;
  int C_size = ldc * B_num_cols;
  // instantiating data
  // int   hA_csrOffsets[] = { 0, 3, 4, 7, 9 };
  // int   hA_columns[]    = { 0, 2, 3, 1, 0, 2, 3, 1, 3 };
  // float hA_values[]     = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f,
  //                           6.0f, 7.0f, 8.0f, 9.0f };
  // float hB[]            = { 1.0f,  2.0f,  3.0f,  4.0f,
  //                           5.0f,  6.0f,  7.0f,  8.0f,
  //                           9.0f, 10.0f, 11.0f, 12.0f };
  // float hC[]            = { 0.0f, 0.0f, 0.0f, 0.0f,
  //                           0.0f, 0.0f, 0.0f, 0.0f,
  //                           0.0f, 0.0f, 0.0f, 0.0f };
  float *hB = (float *)malloc(sizeof(float) * B_size);
  float *hC;
  if (enable_dump) {
    hC = (float *)malloc(sizeof(float) * C_size);
  }
  generate_random_matrix(hB, B_size);
  cusp::csr_matrix<int, float, cusp::host_memory> hA =
      generate_random_sparse_matrix<
          cusp::csr_matrix<int, float, cusp::host_memory>>(A_num_rows,
                                                           A_num_cols, A_nnz);
  cusp::csr_matrix<int, float, cusp::device_memory> dA(hA);
  A_nnz = hA.values.size();
  printf(
      "actual A_nnz due to deduplication during random data generation: %d\n",
      A_nnz);
  float alpha = 1.0f;
  float beta = 0.0f;
  //--------------------------------------------------------------------------

  // Device memory management
  // int   *dA_csrOffsets, *dA_columns;
  // float *dA_values;
  float *dB, *dC;
  // CHECK_CUDA( cudaMalloc((void**) &dA_csrOffsets,
  //                        (A_num_rows + 1) * sizeof(int)) )
  // CHECK_CUDA( cudaMalloc((void**) &dA_columns, A_nnz * sizeof(int))    )
  // CHECK_CUDA( cudaMalloc((void**) &dA_values,  A_nnz * sizeof(float))  )
  CHECK_CUDA(cudaMalloc((void **)&dB, B_size * sizeof(float)))
  CHECK_CUDA(cudaMalloc((void **)&dC, C_size * sizeof(float)))

  // CHECK_CUDA( cudaMemcpy(dA_csrOffsets, hA_csrOffsets,
  //                        (A_num_rows + 1) * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_columns, hA_columns, A_nnz * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_values, hA_values, A_nnz * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  CHECK_CUDA(cudaMemcpy(dB, hB, B_size * sizeof(float), cudaMemcpyHostToDevice))
  // CHECK_CUDA( cudaMemcpy(dC, hC, C_size * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  CHECK_CUDA(cudaMemset(dB, 0, B_size * sizeof(float)))
  //--------------------------------------------------------------------------
  // CUSPARSE APIs
  cusparseHandle_t handle = NULL;
  cusparseSpMatDescr_t matA;
  cusparseDnMatDescr_t matB, matC;
  void *dBuffer = NULL;
  size_t bufferSize = 0;
  CHECK_CUSPARSE(cusparseCreate(&handle))
  // Create sparse matrix A in CSR format
  CHECK_CUSPARSE(cusparseCreateCsr(
      &matA, A_num_rows, A_num_cols, A_nnz,
      // dA_csrOffsets, dA_columns, dA_values,
      (void *)thrust::raw_pointer_cast(dA.row_offsets.data()),
      (void *)thrust::raw_pointer_cast(dA.column_indices.data()),
      (void *)thrust::raw_pointer_cast(dA.values.data()), CUSPARSE_INDEX_32I,
      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_32F))
  // Create dense matrix B
  CHECK_CUSPARSE(cusparseCreateDnMat(&matB, A_num_cols, B_num_cols, ldb, dB,
                                     CUDA_R_32F, CUSPARSE_ORDER_COL))
  // Create dense matrix C
  CHECK_CUSPARSE(cusparseCreateDnMat(&matC, A_num_rows, B_num_cols, ldc, dC,
                                     CUDA_R_32F, CUSPARSE_ORDER_COL))
  // allocate an external buffer if needed
  CHECK_CUSPARSE(cusparseSpMM_bufferSize(
      handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
      CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA, matB, &beta, matC,
      CUDA_R_32F, CUSPARSE_SPMM_ALG_DEFAULT, &bufferSize))
  CHECK_CUDA(cudaMalloc(&dBuffer, bufferSize))

  // execute SpMM
  // We nest the cuda event timing with std::chrono to make sure the cuda event
  // is getting correct results, we will use the cuda event timing results and
  // ignore the std::chrono results
  std::chrono::time_point<std::chrono::system_clock> beg, end;
  cudaEvent_t start, stop;
  CHECK_CUDA(cudaEventCreate(&start));
  CHECK_CUDA(cudaEventCreate(&stop));
  CHECK_CUDA(cudaDeviceSynchronize());

  beg = std::chrono::system_clock::now();
  CHECK_CUDA(cudaEventRecord(start));
  CHECK_CUSPARSE(cusparseSpMM(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                              CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA,
                              matB, &beta, matC, CUDA_R_32F,
                              CUSPARSE_SPMM_ALG_DEFAULT, dBuffer))
  CHECK_CUDA(cudaEventRecord(stop));
  CHECK_CUDA(cudaDeviceSynchronize());
  end = std::chrono::system_clock::now();
  float elapsed_time = 0.0f;
  CHECK_CUDA(cudaEventElapsedTime(&elapsed_time, start, stop));
  printf("cusparseSpMM elapsed time (ms): %f\n", elapsed_time);
  printf("throughput (GFLOPS): %f\n",
         (2.0 * A_nnz * B_num_cols) / (elapsed_time / 1000.0) / 1e9);

  printf(
      "[DEBUG] cusparseSpMM chrono time (microseconds): %ld\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());

  // destroy matrix/vector descriptors
  CHECK_CUSPARSE(cusparseDestroySpMat(matA))
  CHECK_CUSPARSE(cusparseDestroyDnMat(matB))
  CHECK_CUSPARSE(cusparseDestroyDnMat(matC))
  CHECK_CUSPARSE(cusparseDestroy(handle))

  if (enable_dump) {
    CHECK_CUDA(
        cudaMemcpy(hC, dC, C_size * sizeof(float), cudaMemcpyDeviceToHost))
    // Get current timestamp
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d-%H-%M", &tm);
    const char *result_path_and_prefix;
    if (flag_specify_result_path_and_prefix) {
      result_path_and_prefix = cli_result_path_and_prefix;
    } else {
      result_path_and_prefix =
          (std::string("cusparse_bench_spmm_csr.") + time_str).c_str();
    }
    // Store m, n, k to a txt and store A, B, C to a numpy file
    FILE *fp =
        fopen((std::string(result_path_and_prefix) + ".txt").c_str(), "w");
    assert(fp != nullptr);
    fprintf(fp, "%d %d %d %d %f\n", A_num_rows, A_num_cols, B_num_cols, A_nnz,
            A_sparsity);
    fclose(fp);
    cusp::io::write_matrix_market_file(
        hA, std::string(result_path_and_prefix) + ".A.mtx");

    unsigned long b_shape[2] = {ldb, B_num_cols};
    unsigned long c_shape[2] = {ldc, B_num_cols};
    npy::SaveArrayAsNumpy(std::string(result_path_and_prefix) + ".B.npy", false,
                          2, b_shape, hB);
    npy::SaveArrayAsNumpy(std::string(result_path_and_prefix) + ".C.npy", false,
                          2, c_shape, hC);
    free(hC);

    //--------------------------------------------------------------------------
    // device result check
    // int correct = 1;
    // for (int i = 0; i < A_num_rows; i++) {
    //     for (int j = 0; j < B_num_cols; j++) {
    //         if (hC[i + j * ldc] != hC_result[i + j * ldc]) {
    //             correct = 0; // direct floating point comparison is not
    //             reliable break;
    //         }
    //     }
    // }
    // if (correct)
    //     printf("spmm_csr_example test PASSED\n");
    // else
    //     printf("spmm_csr_example test FAILED: wrong result\n");
    //--------------------------------------------------------------------------
  }
  // device memory deallocation
  CHECK_CUDA(cudaFree(dBuffer))
  // CHECK_CUDA( cudaFree(dA_csrOffsets) )
  // CHECK_CUDA( cudaFree(dA_columns) )
  // CHECK_CUDA( cudaFree(dA_values) )
  CHECK_CUDA(cudaFree(dB))
  CHECK_CUDA(cudaFree(dC))
  free(hB);
  return EXIT_SUCCESS;
}