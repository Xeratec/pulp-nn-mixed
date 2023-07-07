/*
 * xpulp_nn_linear_u8_u8_i8.c
 * Nazareno Bruschi <nazareno.bruschi@unibo.it>
 *
 * Copyright (C) 2019-2020 University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "pmsis.h"
#include "pulp_nn_utils.h"



void __attribute__((noinline)) xpulp_nn_linear_u8_u8_i8(
                        uint8_t *pIn,
                        int8_t *pBias,
                        uint8_t *pOut,
                        int8_t *pWeight,
                        int64_t *pKappa,
                        int64_t *pLambda,
                        uint16_t out_mult,
                        uint16_t out_shift,
                        uint16_t dim_vec,
                        uint16_t num_o_neurons,
                        uint8_t flag_relu,
                        uint8_t flag_batch_norm)
{
  uint16_t dim_vec_in = PACK_INT8_SIZE(dim_vec);
  uint16_t dim_vec_wt = PACK_INT8_SIZE(dim_vec);

  int core_id = pi_core_id();
  int Log2Core = log2(NUM_CORES);
  int chunk = (num_o_neurons >> Log2Core) + ((num_o_neurons & (NUM_CORES-1))!=0);
  int start = min(chunk * core_id, num_o_neurons);
  int stop = min(start + chunk, num_o_neurons);

  v4u vecB[1];
  v4s vecA[1];

  uint8_t *pOutBuffer = (uint8_t *) pOut + start;

  int i;

  int64_t *k1 = pKappa + start;
  int64_t *lambda1 = pLambda + start;

  for(i=start; i<stop; i++)
  {
    int sum = 0;

    if (pBias != NULL)
    {
      sum = *(int32_t *)(pBias + 4*i);
    }

    uint8_t *pB = pIn;
    int8_t *pA = pWeight + (i * dim_vec_wt);
    int32_t *ptrA  = (int32_t *) pA ;

    uint32_t *ptrB  = (uint32_t *) pB ;

    ptrA  = MacLoadInit(1, 0, 0, 0, ptrA);

    ptrB  = MacLoadInit(0, 1, 0, 0, ptrB);


    for(int j=0; j < (dim_vec >> 2); j++)
    {
      sum = MacLoad4(1, 0, 0, 0, ptrA, sum);
      ptrA = MacLoadUpdate(ptrA);

      ptrB  = MacLoadInit(0, 1, 0, 0, ptrB);

      //ensure enough instructions in the HW loop - otherwise it will work on GVSOC but not in real hardware!
      asm volatile("nop;");
    }
    uint16_t col_cnt = dim_vec & 0x3;
    if(col_cnt)
    {
      pA=((dim_vec >> 2) << 2);
      pB=((dim_vec >> 2) << 2);
      do
      {
        uint8_t inB = *pB;
        pB++;
        int8_t inA = *pA;
        pA++;
        sum += inA * inB;
        col_cnt--;
      }while (col_cnt);
    }
    if (flag_batch_norm && flag_relu)
    {
      *pOutBuffer = pulp_nn_bn_quant_u8(sum, *k1, *lambda1, out_shift);
      pOutBuffer++;
    }
    else
    {
      if (flag_relu == 1)
      {
        *pOutBuffer = pulp_nn_quant_u8(sum, out_mult, out_shift);
        pOutBuffer++;
      }
      else
      {
        *pOutBuffer = (uint8_t) clip8(sum >> out_shift);
        pOutBuffer++;
      }
    }
  }
  pi_cl_team_barrier(0);
}
