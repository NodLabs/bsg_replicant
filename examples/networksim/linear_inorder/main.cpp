// Copyright (c) 2019, University of Washington All rights reserved.
//
// Redistribution and use in source and binary forms, with or without modification,
// are permitted provided that the following conditions are met:
//
// Redistributions of source code must retain the above copyright notice, this list
// of conditions and the following disclaimer.
//
// Redistributions in binary form must reproduce the above copyright notice, this
// list of conditions and the following disclaimer in the documentation and/or
// other materials provided with the distribution.
//
// Neither the name of the copyright holder nor the names of its contributors may
// be used to endorse or promote products derived from this software without
// specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
// ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
// WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
// DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
// ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
// (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
// LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
// ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#define BSG_MANYCORE_DPI_TILE_PACKET_RX_INORDER

#include <bsg_manycore.h>
#include <bsg_manycore_npa.h>
#include <bsg_manycore_tile.h>
#include <bsg_manycore_vcache.h>
#include <bsg_manycore_dpi_tile.hpp>
#include <cl_manycore_regression.h>

#include <random>
#include <limits>

#ifdef VCS
int vcs_main(int argc, char ** argv) {
#else
int main(int argc, char ** argv) {
#endif
        bsg_pr_test_info(__FILE__ " Regression Test \n");
        bool finish = false;
        hb_mc_packet_t pkt;
        int err;
        hb_mc_manycore_t manycore = {0}, *mc = &manycore;
        hb_mc_coordinate_t origin, dim, max, tg, dram, target;
        hb_mc_npa_t npa;
        uint32_t write_data = 0, read_data = 0;

        unsigned int nels, niters, pto, stride;

        hb_mc_eva_t dram_eva;
        size_t sz = 0;
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_init(mc, __FILE__, 0));

        const hb_mc_config_t *cfg = hb_mc_manycore_get_config(mc);
        origin = hb_mc_config_get_origin_vcore(cfg);
        dim = hb_mc_config_get_dimension_vcore(cfg);

        tg.x = dim.x;
        tg.y = dim.y;

        max.x = origin.x + tg.x - 1;
        max.y = origin.y + tg.y - 1;


        // Experiment configuration:
        //   Number of buffer elements
        //   Per-tile start offset
        //   Number of elements to stride
        //   Number of iterations
        nels = 1024;

        // Each tile will start at index: (pto * (x + y * dim.x)) % nels
        pto = 0;
        stride = 1;
        niters = 1024;

        float initial = 10000;
        float data[nels];
        float expected[max.x + 1][max.y + 1];

        std::default_random_engine generator;
        generator.seed(42);
        std::uniform_real_distribution<float> distribution(.9, 1.0/.9);
        for (uint32_t i = 0 ; i < nels; ++i){
                auto res = distribution(generator);
                data[i] = (res);
        }

        for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                        for(uint32_t j = 0; j < niters; ++j){
                                expected[x_i][y_i] = initial;
                        }
                }
        }

        hb_mc_idx_t idx = 0;
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        for(uint32_t j = 0; j < niters; ++j){
                                expected[x_i][y_i] = expected[x_i][y_i] / data[(j * stride + pto * idx) % nels];
                        }
                        idx ++;
                }
        }

        // Construct DRAM EVA for cache at index 0.
        dram = hb_mc_config_get_dram_coordinate(cfg, 0);
        npa.x = dram.x;
        npa.y = dram.y;
        npa.epa = HB_MC_VCACHE_EPA_BASE;
        BSG_MANYCORE_CALL(mc, hb_mc_npa_to_eva(mc, &default_map, &origin, &npa, &dram_eva, &sz));

        // Write metadata to DMEM on each tile
        bsg_pr_test_info("Writing metadata to DMEM of each tile\n");
        idx = 0;
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        npa.x = x_i;
                        npa.y = y_i;
                        target.x = x_i;
                        target.y = y_i;

                        // DRAM Pointer to buffer
                        write_data = dram_eva;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 0;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Number of elements in buffer
                        write_data = nels;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 4;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Per-Tile Offset into buffer
                        write_data = pto * idx;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 8;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Linear stride multiple
                        write_data = stride;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 12;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Iteration counter (for the tile)
                        write_data = 0;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 16;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Iteration limit (for the tile)
                        write_data = niters;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 20;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Tile Group X
                        write_data = tg.x;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 24;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Tile Group Y
                        write_data = tg.y;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + 28;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Initial value / Final result
                        write_data = *reinterpret_cast<uint32_t*>(&initial);
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + hb_mc_config_get_dmem_size(cfg) - 4;
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_write_mem(mc, &npa, &write_data, sizeof(write_data)));

                        // Set the origin of the target tile
                        BSG_MANYCORE_CALL(mc, hb_mc_tile_set_origin(mc, &target, &origin));
                        idx ++;
                }
        }

        // Write Buffer to DRAM
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_eva_write(mc, &default_map, &origin, &dram_eva, reinterpret_cast<uint32_t*>(data), nels * sizeof(data[0])));

        // Fence to make sure host credits return to their origin.
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_host_request_fence(mc, -1));

        // Enable Tracing
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_trace_enable(mc));

        bsg_pr_test_info("Unfreezing target(s)...\n");
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        target.x = x_i;
                        target.y = y_i;
                        BSG_MANYCORE_CALL(mc, hb_mc_tile_unfreeze(mc, &target));
                }
        }

        // The target(s) will now iterate through the array

        // Wait for finish from all tiles.
        bsg_pr_test_info("Waiting for finish packet...\n");
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_wait_finish(mc, -1));
                }
        }

        // Enable Tracing
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_trace_disable(mc));

        bsg_pr_test_info("Reading result from DMEM\n");
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        npa.x = x_i;
                        npa.y = y_i;
                        target.x = x_i;
                        target.y = y_i;
                        npa.epa = HB_MC_TILE_EPA_DMEM_BASE + hb_mc_config_get_dmem_size(cfg) - 4;

                        BSG_MANYCORE_CALL(mc, hb_mc_manycore_read_mem(mc, &npa, &read_data, sizeof(read_data)));

                        float *val = reinterpret_cast<float *>(&read_data);
                        float result = expected[x_i][y_i];
                        if(*val != result){
                                bsg_pr_err("%s (X:%d, y:%d): Read data (%f) and expected (%f) do not match\n",
                                           __func__, target.x, target.y, *val, result);
                        }
                }
        }

        // Freeze the tile
        bsg_pr_test_info("Freezing Tiles\n");
        for(hb_mc_idx_t y_i = origin.y; y_i <= max.y; ++y_i){
                for(hb_mc_idx_t x_i = origin.x; x_i <= max.x; ++x_i){
                        target.x = x_i;
                        target.y = y_i;
                        BSG_MANYCORE_CALL(mc, hb_mc_tile_freeze(mc, &target));
                }
        }

        // Fence to make sure host credits return to their origin.
        BSG_MANYCORE_CALL(mc, hb_mc_manycore_host_request_fence(mc, -1));

        BSG_MANYCORE_CALL(mc, hb_mc_manycore_exit(mc));
        bsg_pr_test_pass_fail(true);
        return HB_MC_SUCCESS;
};


// This method executes requests to dmem, icache, and csr-space like
// any normal tile.
void BsgDpiTile::execute_request(const hb_mc_request_packet_t *req,
                                 hb_mc_response_packet_t *rsp){
        this->default_request_handler(req, rsp);
}


// This is the traffic generator method. It will not be called until
// the tile is unfrozen, and then it will be called on every cycle
// that a packet can be sent.

// WARNING: Do not use static variables for iteration, they
// are shared between all instances of a class (unless the
// templates are different)

// Instead, store iteration variables in DMEM.
int idx = 0; // Only used by origin
void BsgDpiTile::send_request(bool *req_v_o, hb_mc_request_packet_t *req_o){
        uint32_t *dmem_p = reinterpret_cast<uint32_t *>(this->dmem);
        // The host writes these values before unfreezing the tile.
        hb_mc_eva_t &base = dmem_p[0];
        uint32_t &nels = dmem_p[1];
        uint32_t &offset = dmem_p[2];
        uint32_t &stride = dmem_p[3];
        // iter is our iteration variable. It is set by the host.
        uint32_t &iter = dmem_p[4];
        uint32_t &limit = dmem_p[5];
        hb_mc_coordinate_t tg_dim = {.x = dmem_p[6], .y = dmem_p[7]};

        if(wait_at_barrier(0, tg_dim))
                return;

        if(is_origin() && idx == 0){
                *req_v_o = get_packet_stat_kernel_start(req_o);
                idx ++;
                bsg_pr_info("Start Cycle: %lu\n", get_cycle());
                return;
        }
        if(is_origin() && idx == 1){
                if(fence()){
                        return;
                }
                idx ++;
        }

        // This if statement is effectively a loop, since the method
        // is called on every cycle. Returning is effectively a
        // python-esque yield statement.
        if(iter < limit){
                *req_v_o = get_packet_from_eva<uint32_t>(req_o, base + ((iter*stride + offset) % nels) * sizeof(uint32_t));
                // You MUST increment before returning.
                iter ++;
                return;
        }

        // Wait until all requests have returned, so that the data is
        // accumulated. Do not send packets while we are fencing.
        if(fence())
                return;

        if(wait_at_barrier(1, tg_dim))
                return;

        if(is_origin() && idx == 2){
                *req_v_o = get_packet_stat_kernel_start(req_o);
                idx ++;
                return;
        }
        bsg_pr_info("Finish Cycle: %lu\n", get_cycle());

        // Send the finish packet, once
        *req_v_o = get_packet_finish(req_o);

        // Setting finished to true means this method will no longer
        // be called.
        finished = true;
        return;
}

void BsgDpiTile::receive_response(const hb_mc_response_packet_t *rsp_i){
        float *dmem_p = reinterpret_cast<float *>(this->dmem);
        float val = *reinterpret_cast<const float *>(&rsp_i->data);

        dmem_p[dmem_sz/sizeof(*dmem_p) - 1] = dmem_p[dmem_sz/sizeof(*dmem_p) - 1] / val;
}

