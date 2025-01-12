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
#include <cuda_runtime_api.h>  // cudaMalloc, cudaMemcpy, etc.
#include <cusp/coo_matrix.h>   // cusp::csr_matrix
#include <cusparse.h>          // cusparseSpMM
#include <stdio.h>             // printf
#include <stdlib.h>            // EXIT_FAILURE
#include <utils/generate_random_data.h>
#include <utils/helper_string.h>

#include <chrono>

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

int main(const int argc, const char **argv) {
  // Host problem definition
  // int   A_num_rows   = 4;
  // int   A_num_cols   = 4;
  // int   A_nnz        = 9;
  // int   B_num_cols   = 3;
  // int   num_batches  = 2;

  int A_num_rows = getCmdLineArgumentInt(argc, argv, "A_num_rows");
  int A_num_cols = getCmdLineArgumentInt(argc, argv, "A_num_cols");
  int B_num_cols = getCmdLineArgumentInt(argc, argv, "B_num_cols");
  float A_sparsity = getCmdLineArgumentFloat(argc, argv, "A_sparsity");
  int num_batches = getCmdLineArgumentInt(argc, argv, "num_batches");

  if (argc != 6) {
    printf(
        "Usage: %s --A_num_rows=## --A_num_cols=## --B_num_cols=## "
        "--A_sparsity=0.## --num_batches=##\n",
        argv[0]);
    return EXIT_FAILURE;
  }
  printf("A_num_rows: %d\n", A_num_rows);
  printf("A_num_cols: %d\n", A_num_cols);
  printf("B_num_cols: %d\n", B_num_cols);
  printf("A_sparsity: %f\n", A_sparsity);
  printf("num_batches: %d\n", num_batches);
  // ***** END OF HOST PROBLEM DEFINITION *****

  int B_num_rows = A_num_cols;
  int ldb = B_num_rows;
  int ldc = A_num_rows;
  int B_size = ldb * B_num_cols;
  int C_size = ldc * B_num_cols;
  int A_nnz = A_num_rows * A_num_cols * A_sparsity;

  // initializing data
  // int   hA_rows[]     = { 0, 0, 0, 1, 2, 2, 2, 3, 3 };
  // int   hA_columns1[] = { 0, 2, 3, 1, 0, 2, 3, 1, 3 };
  // int   hA_columns2[] = { 1, 2, 3, 0, 0, 1, 3, 1, 2 };
  // float hA_values1[]  = { /*0*/ 1.0f, 2.0f, 3.0f,
  //                         4.0f, /*0*/ /*0*/ /*0*/
  //                         5.0f, /*0*/ 6.0f, 7.0f,
  //                         /*0*/ 8.0f, /*0*/ 9.0f };
  // float hA_values2[]  = { /*0*/ 10.0f,  11.0f, 12.0f,
  //                         13.0f, /*0*/  /*0*/ /*0*/
  //                         14.0f, 15.0f, /*0*/ 16.0f,
  //                         /*0*/ 17.0f, 18.0f  /*0*/ };
  // float hB1[]         = { 1.0f,  2.0f,  3.0f,  4.0f,
  //                         5.0f,  6.0f,  7.0f,  8.0f,
  //                         9.0f, 10.0f, 11.0f, 12.0f };
  // float hB2[]         = { 6.0f,  4.0f,  3.0f,  2.0f,
  //                         1.0f,  6.0f,  9.0f,  8.0f,
  //                         9.0f, 3.0f,   2.0f,  5.0f };

  // float hC1[]         = { 0.0f, 0.0f, 0.0f, 0.0f,
  //                         0.0f, 0.0f, 0.0f, 0.0f,
  //                         0.0f, 0.0f, 0.0f, 0.0f };
  // float hC2[]         = { 0.0f, 0.0f, 0.0f, 0.0f,
  //                         0.0f, 0.0f, 0.0f, 0.0f,
  //                         0.0f, 0.0f, 0.0f, 0.0f };
  float *hB = (float *)malloc(B_size * sizeof(float));
  generate_random_matrix(hB, B_size);
  cusp::coo_matrix<int, float, cusp::host_memory> hA =
      generate_random_sparse_matrix<
          cusp::coo_matrix<int, float, cusp::host_memory>>(A_num_rows,
                                                           A_num_cols, A_nnz);
  A_nnz = hA.values.size();
  printf(
      "actual A_nnz due to deduplication during random data generation: %d\n",
      A_nnz);

  float alpha = 1.0f;
  float beta = 0.0f;
  //--------------------------------------------------------------------------
  cusp::coo_matrix<int, float, cusp::device_memory> dA(hA);
  // Device memory management
  int *dA_rows, *dA_columns;
  float *dA_values, *dB, *dC;
  CHECK_CUDA(cudaMalloc((void **)&dA_rows, A_nnz * num_batches * sizeof(int)))
  CHECK_CUDA(
      cudaMalloc((void **)&dA_columns, A_nnz * num_batches * sizeof(int)))
  CHECK_CUDA(
      cudaMalloc((void **)&dA_values, A_nnz * num_batches * sizeof(float)))
  CHECK_CUDA(cudaMalloc((void **)&dB, B_size * num_batches * sizeof(float)))
  CHECK_CUDA(cudaMalloc((void **)&dC, C_size * num_batches * sizeof(float)))

  // CHECK_CUDA( cudaMemcpy(dA_rows, hA_rows, A_nnz * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_rows + A_nnz, hA_rows, A_nnz * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_columns, hA_columns1, A_nnz * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_columns + A_nnz, hA_columns2, A_nnz *
  // sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_values, hA_values1, A_nnz * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dA_values + A_nnz, hA_values2, A_nnz *
  // sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  for (int idx = 0; idx < num_batches; idx++) {
    CHECK_CUDA(
        cudaMemcpy(dA_rows + idx * A_nnz,
                   (void *)thrust::raw_pointer_cast(dA.row_indices.data()),
                   A_nnz * sizeof(int), cudaMemcpyDeviceToDevice))
    CHECK_CUDA(
        cudaMemcpy(dA_columns + idx * A_nnz,
                   (void *)thrust::raw_pointer_cast(dA.column_indices.data()),
                   A_nnz * sizeof(int), cudaMemcpyDeviceToDevice))
    CHECK_CUDA(cudaMemcpy(dA_values + idx * A_nnz,
                          (void *)thrust::raw_pointer_cast(dA.values.data()),
                          A_nnz * sizeof(float), cudaMemcpyDeviceToDevice))
    CHECK_CUDA(cudaMemcpy(dB + idx * B_size, hB, B_size * sizeof(float),
                          cudaMemcpyHostToDevice))
  }
  // CHECK_CUDA( cudaMemcpy(dB + B_size, hB2, B_size * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  CHECK_CUDA(cudaMemset(dC, 0, num_batches * C_size * sizeof(float)))
  // CHECK_CUDA( cudaMemcpy(dC, hC1, C_size * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dC + C_size, hC2, C_size * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  //--------------------------------------------------------------------------
  // CUSPARSE APIs
  cusparseHandle_t handle = NULL;
  cusparseSpMatDescr_t matA;
  cusparseDnMatDescr_t matB, matC;
  void *dBuffer = NULL;
  size_t bufferSize = 0;
  CHECK_CUSPARSE(cusparseCreate(&handle))
  // Create sparse matrix A in COO format
  CHECK_CUSPARSE(cusparseCreateCoo(
      &matA, A_num_rows, A_num_cols, A_nnz, dA_rows, dA_columns, dA_values,
      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO, CUDA_R_32F))
  CHECK_CUSPARSE(cusparseCooSetStridedBatch(matA, num_batches, A_nnz))
  // Alternatively, the following code can be used for matA broadcast
  // CHECK_CUSPARSE( cusparseCooSetStridedBatch(matA, num_batches, 0) )
  // Create dense matrix B
  CHECK_CUSPARSE(cusparseCreateDnMat(&matB, B_num_rows, B_num_cols, ldb, dB,
                                     CUDA_R_32F, CUSPARSE_ORDER_COL))
  CHECK_CUSPARSE(cusparseDnMatSetStridedBatch(matB, num_batches, B_size))
  // Create dense matrix C
  CHECK_CUSPARSE(cusparseCreateDnMat(&matC, A_num_rows, B_num_cols, ldc, dC,
                                     CUDA_R_32F, CUSPARSE_ORDER_COL))
  CHECK_CUSPARSE(cusparseDnMatSetStridedBatch(matC, num_batches, C_size))
  // allocate an external buffer if needed
  CHECK_CUSPARSE(cusparseSpMM_bufferSize(
      handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
      CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA, matB, &beta, matC,
      CUDA_R_32F, CUSPARSE_SPMM_COO_ALG4, &bufferSize))
  CHECK_CUDA(cudaMalloc(&dBuffer, bufferSize))

  // execute SpMM
  // We nest the cuda event timing with std::chrono to make sure the cuda event
  // is getting correct results, we will use the cuda event timing results and
  // ignore the std::chrono results
  std::chrono::time_point<std::chrono::system_clock> beg, end;
  cudaEvent_t start, stop;
  CHECK_CUDA(cudaEventCreate(&start));
  CHECK_CUDA(cudaEventCreate(&stop));
  cudaDeviceSynchronize();
  beg = std::chrono::system_clock::now();
  CHECK_CUDA(cudaEventRecord(start));
  CHECK_CUSPARSE(cusparseSpMM(handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                              CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA,
                              matB, &beta, matC, CUDA_R_32F,
                              CUSPARSE_SPMM_COO_ALG4, dBuffer))
  CHECK_CUDA(cudaEventRecord(stop));
  CHECK_CUDA(cudaDeviceSynchronize());
  end = std::chrono::system_clock::now();
  float elapsed_time = 0.0f;
  CHECK_CUDA(cudaEventElapsedTime(&elapsed_time, start, stop));
  printf("cusparseSpMM+COO elapsed time (ms): %f\n", elapsed_time);
  printf(
      "cusparseSpMM+COO throughput (GFLOPS): %f\n",
      (2.0 * A_nnz * num_batches * B_num_cols) / (elapsed_time / 1000.0) / 1e9);

  printf(
      "[DEBUG] chrono time (microseconds): %ld\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());

  // destroy matrix/vector descriptors
  CHECK_CUSPARSE(cusparseDestroySpMat(matA))
  CHECK_CUSPARSE(cusparseDestroyDnMat(matB))
  CHECK_CUSPARSE(cusparseDestroyDnMat(matC))
  CHECK_CUSPARSE(cusparseDestroy(handle))
  //--------------------------------------------------------------------------
  // device result check
  // CHECK_CUDA( cudaMemcpy(hC1, dC, C_size * sizeof(float),
  //                        cudaMemcpyDeviceToHost) )
  // CHECK_CUDA( cudaMemcpy(hC2, dC + C_size, C_size * sizeof(float),
  //                        cudaMemcpyDeviceToHost) )
  // int correct = 1;
  // for (int i = 0; i < A_num_rows; i++) {
  //     for (int j = 0; j < B_num_cols; j++) {
  //         if (hC1[i + j * ldc] != hC1_result[i + j * ldc]) {
  //             correct = 0; // direct floating point comparison is not
  //             reliable break;
  //         }
  //         if (hC2[i + j * ldc] != hC2_result[i + j * ldc]) {
  //             correct = 0;
  //             break;
  //         }
  //     }
  // }
  // if (correct)
  //     printf("spmm_coo_batched_example test PASSED\n");
  // else
  //     printf("spmm_coo_batched_example test FAILED: wrong result\n");
  //--------------------------------------------------------------------------
  // device memory deallocation
  CHECK_CUDA(cudaFree(dBuffer))
  CHECK_CUDA(cudaFree(dA_rows))
  CHECK_CUDA(cudaFree(dA_columns))
  CHECK_CUDA(cudaFree(dA_values))
  CHECK_CUDA(cudaFree(dB))
  CHECK_CUDA(cudaFree(dC))
  return EXIT_SUCCESS;
}
