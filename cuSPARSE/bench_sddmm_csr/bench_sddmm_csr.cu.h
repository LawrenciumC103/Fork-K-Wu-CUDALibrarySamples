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
#include <cusp/csr_matrix.h>
#include <cusparse.h>  // cusparseSpMM
#include <stdio.h>     // printf
#include <stdlib.h>    // EXIT_FAILURE
#include <utils/generate_random_data.h>
#include <utils/helper_string.h>

#include <chrono>
#include <tuple>

#define CHECK_CUDA(func)                                                   \
  {                                                                        \
    cudaError_t status = (func);                                           \
    if (status != cudaSuccess) {                                           \
      printf("CUDA API failed at line %d with error: %s (%d)\n", __LINE__, \
             cudaGetErrorString(status), status);                          \
      exit(EXIT_FAILURE);                                                 \
    }                                                                      \
  }

#define CHECK_CUSPARSE(func)                                                   \
  {                                                                            \
    cusparseStatus_t status = (func);                                          \
    if (status != CUSPARSE_STATUS_SUCCESS) {                                   \
      printf("CUSPARSE API failed at line %d with error: %s (%d)\n", __LINE__, \
             cusparseGetErrorString(status), status);                          \
      exit(EXIT_FAILURE);                                                     \
    }                                                                          \
  }
 
struct BenchSddmmCSRProblemSpec {
  int A_num_rows;
  int A_num_cols;
  int B_num_cols;
  float C_sparsity;
  bool enable_dump;
  char *cli_result_path_and_prefix;
  bool flag_specify_result_path_and_prefix;
};

struct BenchSddmmCSRRuntimeData {
  int C_nnz;
  int B_num_rows;
  int lda;
  int ldb;
  int A_size;
  int B_size;
  float alpha;
  float beta;
  float *hA, *hB;
  float *dB, *dA;
  cusparseHandle_t handle;
  cusparseDnMatDescr_t matA, matB;
  cusparseSpMatDescr_t matC;
  void *dBuffer;
  size_t bufferSize;
  cusp::csr_matrix<int, float, cusp::host_memory> hA;
  cusp::csr_matrix<int, float, cusp::device_memory> dA;
};

std::tuple<BenchSddmmCSRProblemSpec, BenchSddmmCSRRuntimeData>
generate_data_and_prepare(const int argc, const char **argv) {
  // Host problem definition
  int A_num_rows = getCmdLineArgumentInt(argc, argv, "A_num_rows");
  int A_num_cols = getCmdLineArgumentInt(argc, argv, "A_num_cols");
  int B_num_cols = getCmdLineArgumentInt(argc, argv, "B_num_cols");
  float C_sparsity = getCmdLineArgumentFloat(argc, argv, "C_sparsity");
  bool enable_preprocess = checkCmdLineFlag(argc, argv, "enable_preprocess");
  if (A_num_rows == 0 || A_num_cols == 0 || B_num_cols == 0 ||
      C_sparsity == 0) {
    printf(
        "Usage: %s --A_num_rows=## --A_num_cols=## --B_num_cols=## "
        "--C_sparsity=0.## [--enable_preprocess]\n",
        argv[0]);
    exit(EXIT_FAILURE);
  }
  printf("A_num_rows: %d\n", A_num_rows);
  printf("A_num_cols: %d\n", A_num_cols);
  printf("B_num_cols: %d\n", B_num_cols);
  printf("C_sparsity: %f\n", C_sparsity);
  // ***** END OF HOST PROBLEM DEFINITION *****
  int B_num_rows = A_num_cols;
  // int   C_nnz        = 9;
  int C_nnz = A_num_rows * B_num_cols * C_sparsity;
  int lda = A_num_cols;
  int ldb = B_num_cols;
  int A_size = lda * A_num_rows;
  int B_size = ldb * B_num_rows;
  float alpha = 1.0f;
  float beta = 0.0f;
  float *hA, *hB;
  float *dB, *dA;
  cusparseHandle_t handle = NULL;
  cusparseDnMatDescr_t matA, matB;
  cusparseSpMatDescr_t matC;
  void *dBuffer = NULL;
  size_t bufferSize = 0;

  // initializing (instantiating??) data
  // float hA[]         = { 1.0f,   2.0f,  3.0f,  4.0f,
  //                        5.0f,   6.0f,  7.0f,  8.0f,
  //                        9.0f,  10.0f, 11.0f, 12.0f,
  //                        13.0f, 14.0f, 15.0f, 16.0f };
  // float hB[]         = {  1.0f,  2.0f,  3.0f,
  //                         4.0f,  5.0f,  6.0f,
  //                         7.0f,  8.0f,  9.0f,
  //                        10.0f, 11.0f, 12.0f };
  // int   hC_offsets[] = { 0, 3, 4, 7, 9 };
  // int   hC_columns[] = { 0, 1, 2, 1, 0, 1, 2, 0, 2 };
  // float hC_values[]  = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
  //                        0.0f, 0.0f, 0.0f, 0.0f };
  hA = (float *)malloc(sizeof(float) * A_size);
  hB = (float *)malloc(sizeof(float) * B_size); 
  generate_random_matrix(hA, A_size);
  generate_random_matrix(hB, B_size);
  cusp::csr_matrix<int, float, cusp::host_memory> hC =
      generate_random_sparse_matrix_nodup<
          cusp::csr_matrix<int, float, cusp::host_memory>>(A_num_rows,
                                                           B_num_cols, C_nnz);
  //printf(
  //  "actual C_nnz due to deduplication during random data generation: %d\n", C_nnz);
  cusp::csr_matrix<int, float, cusp::device_memory> dC(hC);

  //--------------------------------------------------------------------------
  // Create Handle
  CHECK_CUSPARSE(cusparseCreate(&handle))
  // Device memory management
  // int   *dC_offsets, *dC_columns;
  // float *dC_values,
  float *dB, *dA;
  CHECK_CUDA(cudaMalloc((void **)&dA, A_size * sizeof(float)))
  CHECK_CUDA(cudaMalloc((void **)&dB, B_size * sizeof(float)))
  // CHECK_CUDA( cudaMalloc((void**) &dC_offsets,
  //                        (A_num_rows + 1) * sizeof(int)) )
  // CHECK_CUDA( cudaMalloc((void**) &dC_columns, C_nnz * sizeof(int))   )
  // CHECK_CUDA( cudaMalloc((void**) &dC_values,  C_nnz * sizeof(float)) )

  CHECK_CUDA(cudaMemcpy(dA, hA, A_size * sizeof(float), cudaMemcpyHostToDevice))
  CHECK_CUDA(cudaMemcpy(dB, hB, B_size * sizeof(float), cudaMemcpyHostToDevice))
  // CHECK_CUDA( cudaMemcpy(dC_offsets, hC_offsets,
  //                        (A_num_rows + 1) * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dC_columns, hC_columns, C_nnz * sizeof(int),
  //                        cudaMemcpyHostToDevice) )
  // CHECK_CUDA( cudaMemcpy(dC_values, hC_values, C_nnz * sizeof(float),
  //                        cudaMemcpyHostToDevice) )
  //--------------------------------------------------------------------------
  //201 - 220: Uncertain whether all variable names are correct (Lawrence)
BenchSddmmCSRProblemSpec problem_spec{
      .A_num_rows = A_num_rows,
      .A_num_cols = A_num_cols,
      .B_num_cols = B_num_cols,
      .C_sparsity = C_sparsity,
      .enable_dump = enable_dump,
      .cli_result_path_and_prefix = cli_result_path_and_prefix,
      .flag_specify_result_path_and_prefix =
          flag_specify_result_path_and_prefix,
  };

  BenchSddmmCSRRuntimeData runtime_data{
      .C_nnz = C_nnz,
      .B_num_rows = B_num_rows,
      .lda = lda,
      .ldb = ldb,
      .A_size = A_size,
      .B_size = B_size,
      .alpha = alpha,
      .beta = beta,
      .hB = hB,
      .dB = dB,
      .dC = dC,
      .handle = handle,
      .matA = matA,
      .matB = matB,
      .matC = matC,
      .dBuffer = dBuffer,
      .bufferSize = bufferSize,
      .hA = hA,
      .dA = dA,
  };

  auto bench_tuple = std::make_tuple(problem_spec, runtime_data);
  return bench_tuple;
}

void compute(BenchSddmmCSRProblemSpec problem_spec,
             BenchSddmmCSRRuntimeData runtime_data) {
  // CUSPARSE APIs
  cusparseHandle_t handle = NULL;
  cusparseDnMatDescr_t matA, matB;
  cusparseSpMatDescr_t matC;
  void *dBuffer = NULL;
  size_t bufferSize = 0;
  CHECK_CUSPARSE(cusparseCreate(&handle))
  // Create dense matrix A
  CHECK_CUSPARSE(cusparseCreateDnMat(
      &(runtime_data.matA), problem_spec.A_num_cols, problem_spec.B_num_cols,
      runtime_data.ldA, runtime_data.dA, CUDA_R_32F, CUSPARSE_ORDER_COL))
  // Create dense matrix B
  CHECK_CUSPARSE(cusparseCreateDnMat(
      &(runtime_data.matB), problem_spec.A_num_cols, problem_spec.B_num_cols,
      runtime_data.ldb, runtime_data.dB, CUDA_R_32F, CUSPARSE_ORDER_COL))
  // Create sparse matrix C in CSR format
  CHECK_CUSPARSE(cusparseCreateCsr(
      //original &matC, A_num_rows, B_num_cols, C_nnz,
      &(runtime_data.matC), problem_spec.A_num_rows, problem_spec.B_num_cols,
      runtime_data.C_nnz,
      // dC_offsets, dC_columns, dC_values,
      (void *)thrust::raw_pointer_cast(runtime_data.dC.row_offsets.data()),
      (void *)thrust::raw_pointer_cast(runtime_data.dC.column_indices.data()),
      (void *)thrust::raw_pointer_cast(runtime_data.dC.values.data()),
      CUSPARSE_INDEX_32I, CUSPARSE_INDEX_32I, CUSPARSE_INDEX_BASE_ZERO,
      CUDA_R_32F))

  // allocate an external buffer if needed
  CHECK_CUSPARSE(cusparseSDDMM_bufferSize(
      runtime_data.handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
      CUSPARSE_OPERATION_NON_TRANSPOSE, &(runtime_data.alpha),
      runtime_data.matA, runtime_data.matB, &(runtime_data.beta),
      runtime_data.matC, CUDA_R_32F, CUSPARSE_SDDMM_ALG_DEFAULT,
      &(runtime_data.bufferSize)))
  CHECK_CUDA(cudaMalloc(&(runtime_data.dBuffer), runtime_data.bufferSize))

  // TODO: add option to control if preprocess is enabled
  // execute preprocess (optional)
  if (enable_preprocess) {
    CHECK_CUSPARSE(cusparseSDDMM_preprocess(
        handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
        CUSPARSE_OPERATION_NON_TRANSPOSE, &alpha, matA, matB, &beta, matC,
        CUDA_R_32F, CUSPARSE_SDDMM_ALG_DEFAULT, dBuffer))
  }
  // Execute SpMM
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
  CHECK_CUSPARSE(    
      cusparseSDDMM(runtime_data.handle, CUSPARSE_OPERATION_NON_TRANSPOSE,
                   CUSPARSE_OPERATION_NON_TRANSPOSE, &(runtime_data.alpha),
                   runtime_data.matA, runtime_data.matB, &(runtime_data.beta),
                   runtime_data.matC, CUDA_R_32F, CUSPARSE_SDDMM_ALG_DEFAULT,
                   runtime_data.dBuffer))
  CHECK_CUDA(cudaEventRecord(stop));
  CHECK_CUDA(cudaDeviceSynchronize());
  end = std::chrono::system_clock::now();
  float elapsed_time = 0.0f;
  CHECK_CUDA(cudaEventElapsedTime(&elapsed_time, start, stop));

  printf("cusparseSDDMM+CSR elapsed time (ms): %f\n", elapsed_time);
  printf("cusparseSDDMM+CSR throughput (GFLOPS): %f\n",
         (2.0 * problem_spec.A_num_rows * problem_spec.B_num_cols * problem_spec.A_num_cols) /
             (elapsed_time / 1000.0) / 1e9);
  printf(
      "[DEBUG] cusparseSDDMM chrono time (microseconds): %ld\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());
}
        
  // destroy matrix/vector descriptors
  void cleanup(BenchSddmmCSRProblemSpec problem_spec,
             BenchSddmmSRRuntimeData runtime_data) {
  // Destroy matrix/vector descriptors
  // matA & matB dense, matC sparse 
  CHECK_CUSPARSE(cusparseDestroyDnMat(runtime_data.matA))
  CHECK_CUSPARSE(cusparseDestroyDnMat(runtime_data.matB))
  CHECK_CUSPARSE(cusparseDestroySpMat(runtime_data.matC))
  CHECK_CUSPARSE(cusparseDestroy(runtime_data.handle))

  //--------------------------------------------------------------------------
  // device result check
  // CHECK_CUDA( cudaMemcpy(hC_values,
  // (void*)thrust::raw_pointer_cast(dC.values.data()), C_nnz * sizeof(float),
  //                        cudaMemcpyDeviceToHost) )
  // int correct = 1;
  // for (int i = 0; i < C_nnz; i++) {
  //     if (hC_values[i] != hC_result[i]) {
  //         correct = 0; // direct floating point comparison is not reliable
  //         printf("%d: %f != %f\n", i, hC_values[i], hC_result[i]);
  //         break;
  //     }
  //     else{
  //         printf("%d: %f == %f\n", i, hC_values[i], hC_result[i]);
  //     }
  // }
  // if (correct)
  //     printf("sddmm_csr_example test PASSED\n");
  // else
  //     printf("sddmm_csr_example test FAILED: wrong result\n");
  //--------------------------------------------------------------------------
  // device memory deallocation
  CHECK_CUDA(cudaFree(runtime_data.dBuffer))
  CHECK_CUDA(cudaFree(runtime_data.dA))
  CHECK_CUDA(cudaFree(runtime_data.dB))
  free(runtime_data.hA);
  free(runtime_data.hB);
  return;
  // CHECK_CUDA( cudaFree(dC_offsets) )
  // CHECK_CUDA( cudaFree(dC_columns) )
  // CHECK_CUDA( cudaFree(dC_values) )
}

int main_bench_sddmm_csr(const int argc, const char **argv) {
  auto bench_tuple = generate_data_and_prepare(argc, argv);
  auto bench_spec = std::get<0>(bench_tuple);
  auto bench_data = std::get<1>(bench_tuple);
  compute(bench_spec, bench_data);
  cleanup(bench_spec, bench_data);
}