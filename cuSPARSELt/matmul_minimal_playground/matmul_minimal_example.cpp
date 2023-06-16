/*
 * Copyright 1993-2023 NVIDIA Corporation.  All rights reserved.
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
#include <cusparseLt.h>        // cusparseLt header

#include <cstdio>   // printf
#include <cstdlib>  // std::rand

struct cusparseLtSpMatHandleAndData {
  cusparseLtMatDescriptor_t mat;
  // TODO: the following is associated with the SpMM operator rather than the
  // sparse matrix. Create workspace buffers and pass them to the SpMM
  // execution.
  cusparseLtMatmulAlgSelection_t alg_sel;
  cusparseLtMatmulPlan_t plan;
  cusparseLtMatmulDescriptor_t matmul;

  void *values{nullptr};
};

struct cusparseLtDnMatHandleAndData {
  cusparseLtMatDescriptor_t mat;
  void *values{nullptr};
};

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

constexpr int EXIT_UNSUPPORTED = 2;

int main(void) {
  int major_cc, minor_cc;
  CHECK_CUDA(
      cudaDeviceGetAttribute(&major_cc, cudaDevAttrComputeCapabilityMajor, 0))
  CHECK_CUDA(
      cudaDeviceGetAttribute(&minor_cc, cudaDevAttrComputeCapabilityMinor, 0))
  if (!(major_cc == 8 && minor_cc == 0) && !(major_cc == 8 && minor_cc == 6) &&
      !(major_cc == 8 && minor_cc == 9)) {
    std::printf(
        "\ncusparseLt is supported only on GPU devices with"
        " compute capability == 8.0, 8.6, 8.9 current: %d.%d\n\n",
        major_cc, minor_cc);
    return EXIT_UNSUPPORTED;
  }
  // Host problem definition, row-major order
  // bigger sizes may require dynamic allocations
  constexpr int m = 32;
  constexpr int n = 32;
  constexpr int k = 32;
  auto order = CUSPARSE_ORDER_ROW;
  auto opA = CUSPARSE_OPERATION_NON_TRANSPOSE;
  auto opB = CUSPARSE_OPERATION_NON_TRANSPOSE;
  auto type = CUDA_R_16F;
  auto compute_type = CUSPARSE_COMPUTE_16F;

  bool is_rowmajor = (order == CUSPARSE_ORDER_ROW);
  bool isA_transposed = (opA != CUSPARSE_OPERATION_NON_TRANSPOSE);
  bool isB_transposed = (opB != CUSPARSE_OPERATION_NON_TRANSPOSE);
  auto num_A_rows = (isA_transposed) ? k : m;
  auto num_A_cols = (isA_transposed) ? m : k;
  auto num_B_rows = (isB_transposed) ? n : k;
  auto num_B_cols = (isB_transposed) ? k : n;
  auto num_C_rows = m;
  auto num_C_cols = n;
  unsigned alignment = 16;
  auto lda = (is_rowmajor) ? num_A_cols : num_A_rows;
  auto ldb = (is_rowmajor) ? num_B_cols : num_B_rows;
  auto ldc = (is_rowmajor) ? num_C_cols : num_C_rows;
  auto A_height = (is_rowmajor) ? num_A_rows : num_A_cols;
  auto B_height = (is_rowmajor) ? num_B_rows : num_B_cols;
  auto C_height = (is_rowmajor) ? num_C_rows : num_C_cols;
  auto A_size = A_height * lda * sizeof(__half);
  auto B_size = B_height * ldb * sizeof(__half);
  auto C_size = C_height * ldc * sizeof(__half);
  __half hA[m * k];
  __half hB[k * n];
  __half hC[m * n] = {};
  for (int i = 0; i < m * k; i++)
    hA[i] = static_cast<__half>(static_cast<float>(std::rand() % 10));
  for (int i = 0; i < k * n; i++)
    hB[i] = static_cast<__half>(static_cast<float>(std::rand() % 10));
  float alpha = 1.0f;
  float beta = 0.0f;
  //--------------------------------------------------------------------------
  // Device memory management
  __half *dA, *dB, *dC, *dD, *dA_compressed;
  int *d_valid;
  CHECK_CUDA(cudaMalloc((void **)&dA, A_size))
  CHECK_CUDA(cudaMalloc((void **)&dB, B_size))
  CHECK_CUDA(cudaMalloc((void **)&dC, C_size))
  CHECK_CUDA(cudaMalloc((void **)&d_valid, sizeof(int)))
  dD = dC;

  CHECK_CUDA(cudaMemcpy(dA, hA, A_size, cudaMemcpyHostToDevice))
  CHECK_CUDA(cudaMemcpy(dB, hB, B_size, cudaMemcpyHostToDevice))
  CHECK_CUDA(cudaMemcpy(dC, hC, C_size, cudaMemcpyHostToDevice))
  //--------------------------------------------------------------------------
  cusparseLtHandle_t handle;
  // cusparseLtMatDescriptor_t      matB, matC;
  //  cusparseLtMatmulDescriptor_t   matmul;
  //  cusparseLtMatmulAlgSelection_t alg_sel;
  //  cusparseLtMatmulPlan_t         plan;
  cudaStream_t stream = nullptr;

  void *sh = malloc(sizeof(cusparseLtSpMatHandleAndData));
  auto matA = reinterpret_cast<cusparseLtSpMatHandleAndData *>(sh);
  memset(matA, 0, sizeof(cusparseLtSpMatHandleAndData));
  matA->values = dA;

  void *shb = malloc(sizeof(cusparseLtDnMatHandleAndData));
  auto matB = reinterpret_cast<cusparseLtDnMatHandleAndData *>(shb);
  memset(matB, 0, sizeof(cusparseLtDnMatHandleAndData));
  matB->values = dB;

  void *shc = malloc(sizeof(cusparseLtDnMatHandleAndData));
  auto matC = reinterpret_cast<cusparseLtDnMatHandleAndData *>(shc);
  memset(matC, 0, sizeof(cusparseLtDnMatHandleAndData));
  matC->values = dC;

  CHECK_CUSPARSE(cusparseLtInit(&handle))
  // matrix descriptor initialization
  CHECK_CUSPARSE(cusparseLtStructuredDescriptorInit(
      &handle, &(matA->mat), num_A_rows, num_A_cols, lda, alignment, type,
      order, CUSPARSELT_SPARSITY_50_PERCENT))
  CHECK_CUSPARSE(cusparseLtDenseDescriptorInit(&handle, &(matB->mat),
                                               num_B_rows, num_B_cols, ldb,
                                               alignment, type, order))
  CHECK_CUSPARSE(cusparseLtDenseDescriptorInit(&handle, &(matC->mat),
                                               num_C_rows, num_C_cols, ldc,
                                               alignment, type, order))
  // matmul, algorithm selection, and plan initialization
  CHECK_CUSPARSE(cusparseLtMatmulDescriptorInit(
      &handle, &(matA->matmul), opA, opB, &(matA->mat), &(matB->mat),
      &(matC->mat), &(matC->mat), compute_type))
  CHECK_CUSPARSE(cusparseLtMatmulAlgSelectionInit(
      &handle, &(matA->alg_sel), &(matA->matmul),
      CUSPARSELT_MATMUL_ALG_DEFAULT))
  // CHECK_CUSPARSE( cusparseLtMatmulPlanInit(&handle, &plan, &matmul,
  // &alg_sel))

  //--------------------------------------------------------------------------
  // This step is not needed if the user provides an already-pruned matrix
  // Prune the A matrix (in-place) and check the correctness
  CHECK_CUSPARSE(cusparseLtSpMMAPrune(&handle, &(matA->matmul), dA,
                                      matA->values, CUSPARSELT_PRUNE_SPMMA_TILE,
                                      stream))
  CHECK_CUSPARSE(cusparseLtSpMMAPruneCheck(&handle, &(matA->matmul),
                                           matA->values, d_valid, stream))
  int is_valid;
  CHECK_CUDA(cudaMemcpyAsync(&is_valid, d_valid, sizeof(int),
                             cudaMemcpyDeviceToHost, stream))
  CHECK_CUDA(cudaStreamSynchronize(stream))
  if (is_valid != 0) {
    std::printf(
        "!!!! The matrix has been pruned in a wrong way. "
        "cusparseLtMatmul will not provide correct results\n");
    return EXIT_FAILURE;
  }
  //--------------------------------------------------------------------------
  int num_streams = 0;
  cudaStream_t *streams = nullptr;
  // Search the best kernel
  // CHECK_CUSPARSE( cusparseLtMatmulSearch(&handle, &plan, &alpha,
  //                                        dA_compressed, dB, &beta,
  //                                        dC, dD, nullptr,
  //                                        streams, num_streams) )
  // otherwise, it is possible to set it directly:
  int alg = 0;
  CHECK_CUSPARSE(cusparseLtMatmulAlgSetAttribute(
      &handle, &(matA->alg_sel), CUSPARSELT_MATMUL_ALG_CONFIG_ID, &alg,
      sizeof(alg)))
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  size_t workspace_size;
  CHECK_CUSPARSE(cusparseLtMatmulPlanInit(&handle, &(matA->plan),
                                          &(matA->matmul), &(matA->alg_sel)))

  CHECK_CUSPARSE(
      cusparseLtMatmulGetWorkspace(&handle, &(matA->plan), &workspace_size))
  void *d_workspace;
  CHECK_CUDA(cudaMalloc((void **)&d_workspace, workspace_size))
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // Compress the A matrix
  size_t compressed_size, compressed_buffer_size;
  void *dA_compressedBuffer;
  CHECK_CUSPARSE(cusparseLtSpMMACompressedSize(
      &handle, &(matA->plan), &compressed_size, &compressed_buffer_size))
  CHECK_CUDA(cudaMalloc((void **)&dA_compressed, compressed_size))
  CHECK_CUDA(cudaMalloc((void **)&dA_compressedBuffer, compressed_buffer_size))
  printf("workspace size %ld\n", workspace_size);
  printf("compressed size %ld\n", compressed_size);
  printf("compressed buffer size %ld\n", compressed_buffer_size);
  CHECK_CUSPARSE(cusparseLtSpMMACompress(&handle, &(matA->plan), (matA->values),
                                         dA_compressed, dA_compressedBuffer,
                                         stream))

  // Perform the matrix multiplication
  CHECK_CUSPARSE(cusparseLtMatmul(&handle, &(matA->plan), &alpha, dA_compressed,
                                  (matB->values), &beta, (matC->values), dD,
                                  d_workspace, streams, num_streams))
  //~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
  // destroy plan and handle
  CHECK_CUSPARSE(cusparseLtMatDescriptorDestroy(&(matA->mat)))
  CHECK_CUSPARSE(cusparseLtMatDescriptorDestroy(&(matB->mat)))
  CHECK_CUSPARSE(cusparseLtMatDescriptorDestroy(&(matC->mat)))
  CHECK_CUSPARSE(cusparseLtMatmulPlanDestroy(&(matA->plan)))
  CHECK_CUSPARSE(cusparseLtDestroy(&handle))
  //--------------------------------------------------------------------------
  // device result check
  // matrix A has been pruned
  CHECK_CUDA(cudaMemcpy(hA, dA, A_size, cudaMemcpyDeviceToHost))
  CHECK_CUDA(cudaMemcpy(hC, dC, C_size, cudaMemcpyDeviceToHost))

  bool A_std_layout = (is_rowmajor != isA_transposed);
  bool B_std_layout = (is_rowmajor != isB_transposed);
  // host computation
  float hC_result[m * n];
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      float sum = 0.0f;
      for (int k1 = 0; k1 < k; k1++) {
        auto posA = (A_std_layout) ? i * lda + k1 : i + k1 * lda;
        auto posB = (B_std_layout) ? k1 * ldb + j : k1 + j * ldb;
        sum += static_cast<float>(hA[posA]) *  // [i][k]
               static_cast<float>(hB[posB]);   // [k][j]
      }
      auto posC = (is_rowmajor) ? i * ldc + j : i + j * ldc;
      hC_result[posC] = sum;  // [i][j]
    }
  }
  // host-device comparison
  int correct = 1;
  for (int i = 0; i < m; i++) {
    for (int j = 0; j < n; j++) {
      auto pos = (is_rowmajor) ? i * ldc + j : i + j * ldc;
      auto device_value = static_cast<float>(hC[pos]);
      auto host_value = hC_result[pos];
      if (device_value != host_value) {
        // direct floating point comparison is not reliable
        std::printf("(%d, %d):\t%f vs. %f\n", i, j, host_value, device_value);
        correct = 0;
        break;
      }
    }
  }
  if (correct)
    std::printf("matmul_example test PASSED\n");
  else
    std::printf("matmul_example test FAILED: wrong result\n");
  //--------------------------------------------------------------------------
  // device memory deallocation
  CHECK_CUDA(cudaFree(dA_compressed))
  CHECK_CUDA(cudaFree(dA))
  CHECK_CUDA(cudaFree(dB))
  CHECK_CUDA(cudaFree(dC))
  CHECK_CUDA(cudaFree(d_valid))
  CHECK_CUDA(cudaFree(d_workspace))
  CHECK_CUDA(cudaFree(dA_compressedBuffer))
  return EXIT_SUCCESS;
}