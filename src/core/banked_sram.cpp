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

            // 先按端口顺序计算ready。这里使用请求之间的bank/sub-bank
            // 比较，而不通过运行时bank_idx写入临时占用数组，避免UNROLL后
            // 形成多个动态索引写口。
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

                    for(int previous_port=0;
                            previous_port<NFullRead; ++previous_port){
                        #pragma HLS UNROLL
                        const auto& previous =
                            io.fullRead[(std::size_t)previous_port];
                        const bool previous_valid =
                            previous_port<port && previous.valid &&
                            addressInRange<Rows>(previous.addr);
                        const bool conflict =
                            selected && previous_valid &&
                            getBankIdx<NBanks>(previous.addr)==bank_idx &&
                            previous.subBankMask[(std::size_t)sub_bank];

                        if(conflict){
                            request_ready = false;
                        }
                    }
                }

                request.ready = request_ready;
            }

            // narrow read优先级低于所有full read，也低于更早的narrow read。
            for(int port=0; port<NNarrowRead; ++port){
                #pragma HLS UNROLL
                auto& request = io.narrowRead[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                const int sub_bank_idx = (int)request.subBankIdx.to_uint();
                const bool sub_bank_valid = sub_bank_idx<NSubBanks;
                bool request_ready = address_valid && sub_bank_valid;

                for(int previous_port=0;
                        previous_port<NFullRead; ++previous_port){
                    #pragma HLS UNROLL
                    const auto& previous =
                        io.fullRead[(std::size_t)previous_port];
                    bool previous_selected = false;

                    for(int sub_bank=0;
                            sub_bank<NSubBanks; ++sub_bank){
                        #pragma HLS UNROLL
                        if(sub_bank_idx==sub_bank &&
                                previous.subBankMask[
                                    (std::size_t)sub_bank]){
                            previous_selected = true;
                        }
                    }

                    const bool previous_valid =
                        previous.valid &&
                        addressInRange<Rows>(previous.addr);
                    const bool conflict =
                        sub_bank_valid && previous_valid &&
                        getBankIdx<NBanks>(previous.addr)==bank_idx &&
                        previous_selected;

                    if(conflict){
                        request_ready = false;
                    }
                }

                for(int previous_port=0;
                        previous_port<NNarrowRead; ++previous_port){
                    #pragma HLS UNROLL
                    const auto& previous =
                        io.narrowRead[(std::size_t)previous_port];
                    const int previous_sub_bank_idx =
                        (int)previous.subBankIdx.to_uint();
                    const bool previous_valid =
                        previous_port<port && previous.valid &&
                        addressInRange<Rows>(previous.addr) &&
                        previous_sub_bank_idx<NSubBanks;
                    const bool conflict =
                        sub_bank_valid && previous_valid &&
                        getBankIdx<NBanks>(previous.addr)==bank_idx &&
                        previous_sub_bank_idx==sub_bank_idx;

                    if(conflict){
                        request_ready = false;
                    }
                }

                request.ready = request_ready;
            }

            // write端口使用与read端口相同的固定优先级。
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

                    for(int previous_port=0;
                            previous_port<NFullWrite; ++previous_port){
                        #pragma HLS UNROLL
                        const auto& previous =
                            io.fullWrite[(std::size_t)previous_port];
                        const bool previous_valid =
                            previous_port<port && previous.valid &&
                            addressInRange<Rows>(previous.addr);
                        const bool conflict =
                            selected && previous_valid &&
                            getBankIdx<NBanks>(previous.addr)==bank_idx &&
                            previous.subBankMask[(std::size_t)sub_bank];

                        if(conflict){
                            request_ready = false;
                        }
                    }
                }

                request.ready = request_ready;
            }

            // narrow write优先级低于所有full write和更早的narrow write。
            for(int port=0; port<NNarrowWrite; ++port){
                #pragma HLS UNROLL
                auto& request = io.narrowWrite[(std::size_t)port];
                const bool address_valid = addressInRange<Rows>(request.addr);
                const int bank_idx = getBankIdx<NBanks>(request.addr);
                const int sub_bank_idx = (int)request.subBankIdx.to_uint();
                const bool sub_bank_valid = sub_bank_idx<NSubBanks;
                bool request_ready = address_valid && sub_bank_valid;

                for(int previous_port=0;
                        previous_port<NFullWrite; ++previous_port){
                    #pragma HLS UNROLL
                    const auto& previous =
                        io.fullWrite[(std::size_t)previous_port];
                    bool previous_selected = false;

                    for(int sub_bank=0;
                            sub_bank<NSubBanks; ++sub_bank){
                        #pragma HLS UNROLL
                        if(sub_bank_idx==sub_bank &&
                                previous.subBankMask[
                                    (std::size_t)sub_bank]){
                            previous_selected = true;
                        }
                    }

                    const bool previous_valid =
                        previous.valid &&
                        addressInRange<Rows>(previous.addr);
                    const bool conflict =
                        sub_bank_valid && previous_valid &&
                        getBankIdx<NBanks>(previous.addr)==bank_idx &&
                        previous_selected;

                    if(conflict){
                        request_ready = false;
                    }
                }

                for(int previous_port=0;
                        previous_port<NNarrowWrite; ++previous_port){
                    #pragma HLS UNROLL
                    const auto& previous =
                        io.narrowWrite[(std::size_t)previous_port];
                    const int previous_sub_bank_idx =
                        (int)previous.subBankIdx.to_uint();
                    const bool previous_valid =
                        previous_port<port && previous.valid &&
                        addressInRange<Rows>(previous.addr) &&
                        previous_sub_bank_idx<NSubBanks;
                    const bool conflict =
                        sub_bank_valid && previous_valid &&
                        getBankIdx<NBanks>(previous.addr)==bank_idx &&
                        previous_sub_bank_idx==sub_bank_idx;

                    if(conflict){
                        request_ready = false;
                    }
                }

                request.ready = request_ready;
            }

            // 为每个固定bank/sub-bank只生成一个物理读口。先选择本拍
            // 获胜请求，再执行一次静态bank索引的读取并路由到响应寄存器。
            for(int bank=0; bank<NBanks; ++bank){
                #pragma HLS UNROLL
                for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                    #pragma HLS UNROLL
                    bool read_enable = false;
                    bool full_read_selected = false;
                    int selected_port = 0;
                    sram_address_t read_address = 0;

                    for(int port=0; port<NFullRead; ++port){
                        #pragma HLS UNROLL
                        const auto& request =
                            io.fullRead[(std::size_t)port];
                        const bool selected =
                            request.valid && request.ready &&
                            getBankIdx<NBanks>(request.addr)==bank &&
                            request.subBankMask[(std::size_t)sub_bank];

                        if(!read_enable && selected){
                            read_enable = true;
                            full_read_selected = true;
                            selected_port = port;
                            read_address = request.addr;
                        }
                    }

                    for(int port=0; port<NNarrowRead; ++port){
                        #pragma HLS UNROLL
                        const auto& request =
                            io.narrowRead[(std::size_t)port];
                        const bool selected =
                            request.valid && request.ready &&
                            getBankIdx<NBanks>(request.addr)==bank &&
                            (int)request.subBankIdx.to_uint()==sub_bank;

                        if(!read_enable && selected){
                            read_enable = true;
                            full_read_selected = false;
                            selected_port = port;
                            read_address = request.addr;
                        }
                    }

                    typename State::NarrowData read_data{};
                    if(read_enable){
                        const int address =
                            (int)read_address.to_uint();
                        for(int element=0; element<SubBankSize; ++element){
                            #pragma HLS UNROLL
                            read_data[(std::size_t)element] =
                                state.banks[bank][sub_bank]
                                           [address][element];
                        }
                    }

                    for(int port=0; port<NFullRead; ++port){
                        #pragma HLS UNROLL
                        if(read_enable && full_read_selected &&
                                selected_port==port){
                            for(int element=0;
                                    element<SubBankSize; ++element){
                                #pragma HLS UNROLL
                                const int row_element =
                                    sub_bank*SubBankSize+element;
                                state.full_read_data[(std::size_t)port]
                                    [(std::size_t)row_element] =
                                        read_data[(std::size_t)element];
                            }
                        }
                    }

                    for(int port=0; port<NNarrowRead; ++port){
                        #pragma HLS UNROLL
                        if(read_enable && !full_read_selected &&
                                selected_port==port){
                            state.narrow_read_data[(std::size_t)port] =
                                read_data;
                        }
                    }
                }
            }

            // 同理，每个固定bank/sub-bank只生成一个物理写口。
            // 读选择和数据获取位于写入之前，保持同拍读写返回旧值。
            for(int bank=0; bank<NBanks; ++bank){
                #pragma HLS UNROLL
                for(int sub_bank=0; sub_bank<NSubBanks; ++sub_bank){
                    #pragma HLS UNROLL
                    bool write_enable = false;
                    sram_address_t write_address = 0;
                    typename State::NarrowData write_data{};

                    for(int port=0; port<NFullWrite; ++port){
                        #pragma HLS UNROLL
                        const auto& request =
                            io.fullWrite[(std::size_t)port];
                        const bool selected =
                            request.valid && request.ready &&
                            getBankIdx<NBanks>(request.addr)==bank &&
                            request.subBankMask[(std::size_t)sub_bank];

                        if(!write_enable && selected){
                            write_enable = true;
                            write_address = request.addr;
                            for(int element=0;
                                    element<SubBankSize; ++element){
                                #pragma HLS UNROLL
                                const int row_element =
                                    sub_bank*SubBankSize+element;
                                write_data[(std::size_t)element] =
                                    request.data[(std::size_t)row_element];
                            }
                        }
                    }

                    for(int port=0; port<NNarrowWrite; ++port){
                        #pragma HLS UNROLL
                        const auto& request =
                            io.narrowWrite[(std::size_t)port];
                        const bool selected =
                            request.valid && request.ready &&
                            getBankIdx<NBanks>(request.addr)==bank &&
                            (int)request.subBankIdx.to_uint()==sub_bank;

                        if(!write_enable && selected){
                            write_enable = true;
                            write_address = request.addr;
                            write_data = request.data;
                        }
                    }

                    if(write_enable){
                        const int address =
                            (int)write_address.to_uint();
                        for(int element=0; element<SubBankSize; ++element){
                            #pragma HLS UNROLL
                            state.banks[bank][sub_bank][address][element] =
                                write_data[(std::size_t)element];
                        }
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
