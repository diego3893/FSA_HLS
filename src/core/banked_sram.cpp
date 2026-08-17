#include "fsa/banked_sram.hpp"

namespace fsa{

    namespace{

        /**
         * @brief 判断逻辑行地址是否位于SRAM范围内
         * 
         * @tparam Rows 总行数
         * @param addr 地址
         * @return true 地址合法
         * @return false 地址非法
         */
        template <int Rows>
        bool addressInRange(const sram_address_t& addr){
            return addr.to_uint()<(unsigned int)Rows;
        }

        /**
         * @brief 使用逻辑地址低位选择物理bank
         * 
         * @tparam NBanks bank数
         * @param addr 地址
         * @return int bank编号
         */
        template <int NBanks>
        int getBankIdx(const sram_address_t& addr){
            return (int)(addr.to_uint()&(unsigned int)(NBanks-1));
        }

        /**
         * @brief 复位同步读响应寄存器
         * 
         * @tparam State SRAM状态
         * @tparam NFullRead 整行读端口数量
         * @tparam NNarrowRead 窄读端口数量
         * @param state 
         */
        template <typename State,
                  int NFullRead, int NNarrowRead>
        void resetBankedSRAMState(State& state){
            for(int port=0; port<NFullRead; ++port){
                #pragma HLS UNROLL
                for(int element=0; element<State::FullDataSize; ++element){
                    #pragma HLS UNROLL
                    state.full_read_data[(std::size_t)port][(std::size_t)element] = {};
                }
            }

            for(int port=0; port<NNarrowRead; ++port){
                #pragma HLS UNROLL
                for(int element=0; element<State::NarrowDataSize; ++element){
                    #pragma HLS UNROLL
                    state.narrow_read_data[(std::size_t)port][(std::size_t)element] = {};
                }
            }
        }

        /**
         * @brief BankedSRAM通用逐拍实现
         * @tparam T 元素类型
         * @tparam Rows 行数
         * @tparam RowSize 一行的元素个数
         * @tparam NBanks bank数量
         * @tparam NSubBanks sub-bank个数
         * @tparam NFullRead 整行读端口数量
         * @tparam NFullWrite 整行写端口数量
         * @tparam NNarrowRead 窄读端口数量
         * @tparam NNarrowWrite 窄写端口数量
         * @param state SRAM状态
         * @param io SRAM端口
         */
        template <typename T, int Rows, int RowSize,
                  int NBanks, int NSubBanks,
                  int NFullRead, int NFullWrite,
                  int NNarrowRead, int NNarrowWrite>
        void bankedSRAMStep(
            BankedSRAMState<T, Rows, RowSize,
                            NBanks, NSubBanks,
                            NFullRead, NFullWrite,
                            NNarrowRead, NNarrowWrite>& state,
            BankedSRAMIO<
                T, RowSize, NSubBanks,
                NFullRead, NFullWrite,
                NNarrowRead, NNarrowWrite>& io){
            #pragma HLS INLINE

            using State = BankedSRAMState<
                T, Rows, RowSize,
                NBanks, NSubBanks,
                NFullRead, NFullWrite,
                NNarrowRead, NNarrowWrite>;

            constexpr int SubBankSize = State::SubBankSize;

            // 保持并行性
            #pragma HLS ARRAY_PARTITION variable=state.banks type=complete dim=1
            #pragma HLS ARRAY_PARTITION variable=state.banks type=complete dim=2
            // 组合一个sub-bank内部的sram字
            #pragma HLS ARRAY_RESHAPE variable=state.banks type=complete dim=4

            for(int port=0; port<NFullRead; ++port){
                #pragma HLS UNROLL
                io.fullRead[(std::size_t)port].data =
                    state.full_read_data[(std::size_t)port];
            }

            for(int port=0; port<NNarrowRead; ++port){
                #pragma HLS UNROLL
                io.narrowRead[(std::size_t)port].data =
                    state.narrow_read_data[(std::size_t)port];
            }

            // 当前端口是否被更高请求zhanyong
            bool read_occupied[NBanks][NSubBanks]{};
            bool write_occupied[NBanks][NSubBanks]{};

            #pragma HLS ARRAY_PARTITION variable=read_occupied type=complete dim=0
            #pragma HLS ARRAY_PARTITION variable=write_occupied type=complete dim=0

            // full read仲裁
            for(int port=0; port<NFullRead; ++port){
                #pragma HLS UNROLL
                auto& request = io.fullRead[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                bool request_ready = address_valid;

                for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                    #pragma HLS UNROLL
                    const bool selected =
                        request.subBankMask[(std::size_t)sub_bank];

                    if(selected && read_occupied[bank_idx][sub_bank]){
                        request_ready = false;
                    }

                    if(request.valid && address_valid && selected){
                        read_occupied[bank_idx][sub_bank] = true;
                    }
                }

                request.ready = request_ready;
            }

            // narrow read应该在full read的优先级之后
            for(int port=0; port<NNarrowRead; ++port){
                #pragma HLS UNROLL
                auto& request = io.narrowRead[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                const int sub_bank_idx = (int)request.subBankIdx.to_uint();
                const bool sub_bank_valid = sub_bank_idx<NSubBanks;

                request.ready = address_valid && sub_bank_valid &&
                    !read_occupied[bank_idx][sub_bank_idx];

                if(request.valid && address_valid && sub_bank_valid){
                    read_occupied[bank_idx][sub_bank_idx] = true;
                }
            }

            // full write仲裁
            for(int port=0; port<NFullWrite; ++port){
                #pragma HLS UNROLL
                auto& request = io.fullWrite[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                bool request_ready = address_valid;

                for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                    #pragma HLS UNROLL
                    const bool selected =
                        request.subBankMask[(std::size_t)sub_bank];

                    if(selected && write_occupied[bank_idx][sub_bank]){
                        request_ready = false;
                    }

                    if(request.valid && address_valid && selected){
                        write_occupied[bank_idx][sub_bank] = true;
                    }
                }

                request.ready = request_ready;
            }

            // narrow write应该在full write之后
            for(int port=0; port<NNarrowWrite; ++port){
                #pragma HLS UNROLL
                auto& request = io.narrowWrite[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                const int sub_bank_idx = (int)request.subBankIdx.to_uint();
                const bool sub_bank_valid = sub_bank_idx<NSubBanks;

                request.ready = address_valid && sub_bank_valid &&
                    !write_occupied[bank_idx][sub_bank_idx];

                if(request.valid && address_valid && sub_bank_valid){
                    write_occupied[bank_idx][sub_bank_idx] = true;
                }
            }

            // 同步读，握手成功则保存。先读后写
            for(int port=0; port<NFullRead; ++port){
                #pragma HLS UNROLL
                const auto& request = io.fullRead[(std::size_t)port];

                if(request.valid && request.ready){
                    const int bank_idx = getBankIdx<NBanks>(request.addr);
                    const int address = (int)request.addr.to_uint();

                    for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                        #pragma HLS UNROLL
                        if(request.subBankMask[(std::size_t)sub_bank]){
                            for(int element=0; element<SubBankSize; ++element){
                                #pragma HLS UNROLL
                                const int row_element =
                                    sub_bank*SubBankSize+element;

                                state.full_read_data[(std::size_t)port]
                                    [(std::size_t)row_element] =
                                        state.banks[bank_idx][sub_bank]
                                                   [address][element];
                            }
                        }
                    }
                }
            }

            for(int port=0; port<NNarrowRead; ++port){
                #pragma HLS UNROLL
                const auto& request = io.narrowRead[(std::size_t)port];

                if(request.valid && request.ready){
                    const int bank_idx = getBankIdx<NBanks>(request.addr);
                    const int sub_bank_idx =
                        (int)request.subBankIdx.to_uint();
                    const int address = (int)request.addr.to_uint();

                    for(int element=0; element<SubBankSize; ++element){
                        #pragma HLS UNROLL
                        state.narrow_read_data[(std::size_t)port]
                                              [(std::size_t)element] =
                            state.banks[bank_idx][sub_bank_idx]
                                       [address][element];
                    }
                }
            }

            // valid&&ready的full write一次性更新所有被mask选中的sub-bank
            for(int port=0; port<NFullWrite; ++port){
                #pragma HLS UNROLL
                const auto& request = io.fullWrite[(std::size_t)port];

                if(request.valid && request.ready){
                    const int bank_idx = getBankIdx<NBanks>(request.addr);
                    const int address = (int)request.addr.to_uint();

                    for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                        #pragma HLS UNROLL
                        if(request.subBankMask[(std::size_t)sub_bank]){
                            for(int element=0; element<SubBankSize; ++element){
                                #pragma HLS UNROLL
                                const int row_element =
                                    sub_bank*SubBankSize+element;

                                state.banks[bank_idx][sub_bank]
                                           [address][element] =
                                    request.data[(std::size_t)row_element];
                            }
                        }
                    }
                }
            }

            for(int port=0; port<NNarrowWrite; ++port){
                #pragma HLS UNROLL
                const auto& request = io.narrowWrite[(std::size_t)port];

                if(request.valid && request.ready){
                    const int bank_idx = getBankIdx<NBanks>(request.addr);
                    const int sub_bank_idx =
                        (int)request.subBankIdx.to_uint();
                    const int address = (int)request.addr.to_uint();

                    for(int element=0; element<SubBankSize; ++element){
                        #pragma HLS UNROLL
                        state.banks[bank_idx][sub_bank_idx]
                                   [address][element] =
                            request.data[(std::size_t)element];
                    }
                }
            }
        }

    }  // namespace

    void reset_sp_ram_state(SpRAMState& state){
        #pragma HLS INLINE
        resetBankedSRAMState<SpRAMState, 1, 0>(state);
    }

    void sp_ram_step(SpRAMState& state, SpRAMIO& io){
        #pragma HLS INLINE
        bankedSRAMStep<
            elem_t, SPAD_ROWS, SA_ROWS,
            spadBanks, SPAD_SUB_BANKS,
            1, 0, 0, nMemPorts>(state, io);
    }

    void reset_acc_ram_state(AccRAMState& state){
        #pragma HLS INLINE
        resetBankedSRAMState<AccRAMState, 1, nMemPorts>(state);
    }

    void acc_ram_step(AccRAMState& state, AccRAMIO& io){
        #pragma HLS INLINE
        bankedSRAMStep<
            acc_t, ACC_ROWS, SA_COLS,
            accBanks, ACC_SUB_BANKS,
            1, 1, nMemPorts, 0>(state, io);
    }

}  // namespace fsa
