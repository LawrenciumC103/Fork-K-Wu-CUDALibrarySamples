/*
 * Copyright 2020 NVIDIA Corporation.  All rights reserved.
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
#include <cublas_v2.h>
#include <cuda_runtime.h>
#include <utils/generate_random_data.h>
#include <utils/helper_string.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <vector>

#include "cublas_utils.h"
#include "npy.hpp"

using data_type = float;

int main_bench_gemm(const int argc, const char *argv[]) {
  cublasHandle_t cublasH = NULL;
  cudaStream_t stream = NULL;

  // const int m = 2;
  // const int n = 2;
  // const int k = 2;
  // const int lda = 2;
  // const int ldb = 2;
  // const int ldc = 2;

  // Host problem definition
  int m = getCmdLineArgumentInt(argc, argv, "m");
  int n = getCmdLineArgumentInt(argc, argv, "n");
  int k = getCmdLineArgumentInt(argc, argv, "k");
  bool enable_dump = checkCmdLineFlag(argc, argv, "enable_dump");
  char *cli_result_path_and_prefix;
  bool flag_specify_result_path_and_prefix = getCmdLineArgumentString(
      argc, argv, "result_path_and_prefix", &cli_result_path_and_prefix);
  if (m == 0 || n == 0 || k == 0) {
    printf(
        "Usage: %s --m=## --n=## --k=## [--enable_dump] "
        "[--result_path_and_prefix=...]\n",
        argv[0]);
    return EXIT_FAILURE;
  }
  int lda = m;
  int ldb = k;
  int ldc = m;
  /*
   *   A = | 1.0 | 2.0 |
   *       | 3.0 | 4.0 |
   *
   *   B = | 5.0 | 6.0 |
   *       | 7.0 | 8.0 |
   */

  std::srand(unsigned(std::time(nullptr)));
  std::vector<data_type> A(lda * k);
  std::vector<data_type> B(ldb * n);
  std::generate(A.begin(), A.end(), std::rand);
  std::generate(B.begin(), B.end(), std::rand);
  // const std::vector<data_type> A = {1.0, 2.0, 3.0, 4.0};
  // const std::vector<data_type> B = {5.0, 6.0, 7.0, 8.0};
  std::vector<data_type> C(m * n);
  const data_type alpha = 1.0;
  const data_type beta = 0.0;

  data_type *d_A = nullptr;
  data_type *d_B = nullptr;
  data_type *d_C = nullptr;

  cublasOperation_t transa = CUBLAS_OP_N;
  cublasOperation_t transb = CUBLAS_OP_N;

  if (0) {
    printf("A\n");
    print_matrix(m, k, A.data(), lda);
    printf("=====\n");

    printf("B\n");
    print_matrix(k, n, B.data(), ldb);
    printf("=====\n");
  }

  /* step 1: create cublas handle, bind a stream */
  CUBLAS_CHECK(cublasCreate(&cublasH));

  CUDA_CHECK(cudaStreamCreateWithFlags(&stream, cudaStreamNonBlocking));
  CUBLAS_CHECK(cublasSetStream(cublasH, stream));

  /* step 2: copy data to device */
  CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_A),
                        sizeof(data_type) * A.size()));
  CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_B),
                        sizeof(data_type) * B.size()));
  CUDA_CHECK(cudaMalloc(reinterpret_cast<void **>(&d_C),
                        sizeof(data_type) * C.size()));

  CUDA_CHECK(cudaMemcpyAsync(d_A, A.data(), sizeof(data_type) * A.size(),
                             cudaMemcpyHostToDevice, stream));
  CUDA_CHECK(cudaMemcpyAsync(d_B, B.data(), sizeof(data_type) * B.size(),
                             cudaMemcpyHostToDevice, stream));

  /* step 3: compute */
  // We nest the cuda event timing with std::chrono to make sure the cuda event
  // is getting correct results, we will use the cuda event timing results and
  // ignore the std::chrono results
  std::chrono::time_point<std::chrono::system_clock> beg, end;
  cudaEvent_t start, stop;
  CUDA_CHECK(cudaEventCreate(&start));
  CUDA_CHECK(cudaEventCreate(&stop));
  CUDA_CHECK(cudaDeviceSynchronize());
  CUDA_CHECK(cudaStreamSynchronize(stream));
  CUDA_CHECK(cudaDeviceSynchronize());

  beg = std::chrono::system_clock::now();

  CUDA_CHECK(cudaEventRecord(start, stream));

  CUBLAS_CHECK(cublasSgemm(cublasH, transa, transb, m, n, k, &alpha, d_A, lda,
                           d_B, ldb, &beta, d_C, ldc));
  CUDA_CHECK(cudaEventRecord(stop, stream));
  CUDA_CHECK(cudaStreamSynchronize(stream));
  CUDA_CHECK(cudaDeviceSynchronize());
  end = std::chrono::system_clock::now();
  float elapsed_time = 0.0f;
  CUDA_CHECK(cudaEventElapsedTime(&elapsed_time, start, stop));
  printf("cublas<X>gemm elapsed time (ms): %f\n", elapsed_time);
  printf("throughput (GFLOPS): %f\n",
         (2.0 * m * n * k) / (elapsed_time / 1000.0) / 1e9);
  printf(
      "[DEBUG] cublas<X>gemm chrono time (microseconds): %ld\n",
      std::chrono::duration_cast<std::chrono::microseconds>(end - beg).count());

  if (enable_dump) {
    /* step 4: copy data to host */
    CUDA_CHECK(cudaMemcpyAsync(C.data(), d_C, sizeof(data_type) * C.size(),
                               cudaMemcpyDeviceToHost, stream));
    CUDA_CHECK(cudaStreamSynchronize(stream));

    if (0) {
      /*
       *   C = | 23.0 | 31.0 |
       *       | 34.0 | 46.0 |
       */
      printf("C\n");
      print_matrix(m, n, C.data(), ldc);
      printf("=====\n");
    }
    // Get current timestamp
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%Y-%m-%d-%H-%M", &tm);
    const char *result_path_and_prefix;
    if (!flag_specify_result_path_and_prefix) {
      result_path_and_prefix =
          (std::string("cublas_bench_gemm.") + time_str).c_str();
    } else {
      result_path_and_prefix = cli_result_path_and_prefix;
    }
    result_path_and_prefix = nullptr;
    // Store m, n, k to a txt and store A, B, C to a numpy file
    FILE *fp =
        fopen((std::string(result_path_and_prefix) + ".txt").c_str(), "w");
    assert(fp != nullptr);
    fprintf(fp, "%d %d %d\n", m, n, k);
    fclose(fp);
    unsigned long a_shape[2] = {lda, k};
    unsigned long b_shape[2] = {ldb, n};
    unsigned long c_shape[2] = {m, n};
    npy::SaveArrayAsNumpy(std::string(result_path_and_prefix) + ".C.npy", false,
                          2, c_shape, C);
    npy::SaveArrayAsNumpy(std::string(result_path_and_prefix) + ".A.npy", false,
                          2, a_shape, A);
    npy::SaveArrayAsNumpy(std::string(result_path_and_prefix) + ".B.npy", false,
                          2, b_shape, B);
  }

  /* free resources */
  CUDA_CHECK(cudaFree(d_A));
  CUDA_CHECK(cudaFree(d_B));
  CUDA_CHECK(cudaFree(d_C));

  CUBLAS_CHECK(cublasDestroy(cublasH));

  CUDA_CHECK(cudaStreamDestroy(stream));

  CUDA_CHECK(cudaDeviceReset());

  return EXIT_SUCCESS;
}