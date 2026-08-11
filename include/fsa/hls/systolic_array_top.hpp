#ifndef SYSTOLIC_ARRAY_TOP_HPP
#define SYSTOLIC_ARRAY_TOP_HPP

#include "fsa/types.hpp"
#include "fsa/control.hpp"

namespace fsa{
    struct SystolicArrayInput{
        bool reset = false;
        ValidData<PECtrl> pe_ctrl[SA_ROWS]{};
        ValidData<CmpControl> cmp_ctrl{};
        ElemVector pe_data;
    };

    struct SystolicArrayOutput{
        ValidData<acc_t> acc_out[SA_COLS];
    };
}

void systolic_array_top(const fsa::SystolicArrayInput& input, 
        fsa::SystolicArrayOutput& output);

#endif // !SYSTOLIC_ARRAY_TOP_HPP