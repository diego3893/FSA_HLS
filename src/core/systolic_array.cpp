// TODO: #pragma 修饰是必须的！
#include "fsa/systolic_array.hpp"
#include "fsa/arithmetic.hpp"
#include "fsa/cmp.hpp"
#include "fsa/pe.hpp"

namespace fsa{

    void reset_systolic_array_state(SystolicArrayState& state){
        for(int row=0; row<SA_ROWS; ++row){
            for(int col=0; col<SA_COLS; ++col){
                reset_pe_state(state.mesh[row][col]);
                state.pe_ctrl_pipe[row][col] = ValidData<PECtrl>{};
                state.r_output_pipe[row][col] = ValidData<elem_t>{};
                state.d_output_pipe[row][col] = ValidData<acc_t>{};
                state.u_output_pipe[row][col] = ValidData<acc_t>{};
            }
        }
        for(int col=0; col<SA_COLS; ++col){
            reset_cmp_state(state.cmp_array[col]);
            state.cmp_ctrl_pipe[col] = ValidData<CmpControl>{};
            state.cmp_d_output_pipe[col] = ValidData<acc_t>{};
        }
        return;
    }

    void systolic_array_step(const SystolicArrayState& current,
                            SystolicArrayState& next, SystolicArrayIO& io){
        next = current;

        for(int col=0; col<SA_COLS; ++col){
            io.acc_out[col] = current.d_output_pipe[SA_ROWS-1][col];
        }

        for(int col=0; col<SA_COLS; ++col){
            CMPIO cmp_io{};
            if(col == 0){
                cmp_io.in_ctrl = io.cmp_ctrl;
            }else{
                cmp_io.in_ctrl = current.cmp_ctrl_pipe[col-1];
            }
            cmp_io.d_input = current.u_output_pipe[0][col];

            cmp_step(current.cmp_array[col], next.cmp_array[col], cmp_io);

            next.cmp_ctrl_pipe[col] = cmp_io.out_ctrl;
            next.cmp_d_output_pipe[col] = cmp_io.d_output;
        }

        for(int row=0; row<SA_ROWS; ++row){
            for(int col=0; col<SA_COLS; ++col){
                PEIO pe_io{};

                if(col == 0){
                    pe_io.in_ctrl = io.pe_ctrl[row];
                }else{
                    pe_io.in_ctrl = current.pe_ctrl_pipe[row][col-1];
                }

                if(col == 0){
                    pe_io.l_input = make_valid(io.pe_data[(std::size_t)row]);
                }else{
                    pe_io.l_input = current.r_output_pipe[row][col-1];
                }

                if(row == 0){
                    pe_io.u_input = current.cmp_d_output_pipe[col];
                }else{
                    pe_io.u_input = current.d_output_pipe[row-1][col];
                }

                if(row == SA_ROWS-1){
                    pe_io.d_input = make_valid(accZero());
                }else{
                    pe_io.d_input = current.u_output_pipe[row+1][col];
                }

                pe_step(current.mesh[row][col], next.mesh[row][col], pe_io);

                next.pe_ctrl_pipe[row][col] = pe_io.out_ctrl;
                next.r_output_pipe[row][col] = pe_io.r_output;
                next.d_output_pipe[row][col] = pe_io.d_output;
                next.u_output_pipe[row][col] = pe_io.u_output;
            }
        }
    }

}  // namespace fsa