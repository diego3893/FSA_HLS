/**
 * @file test_banked_sram_top.cpp
 * @brief BankedSRAM两个HLS顶层的功能、时序和仲裁testbench
 *
 * 本测试只调用sp_ram_top和acc_ram_top，不直接访问内部static状态。覆盖：
 * 1. 同步读的一拍响应延迟和空闲事务期间的数据保持；
 * 2. full access的sub-bank mask；
 * 3. 同一物理bank/sub-bank上的端口顺序优先级；
 * 4. 不同bank或不同sub-bank的并行访问；
 * 5. 越界地址拒绝，以及reset清响应但不清SRAM内容。
 */

#include <cstddef>
#include <iostream>
#include <string>

#include "fsa/hls/banked_sram_top.hpp"

namespace{

static_assert(fsa::SPAD_SUB_BANKS==1,
              "当前testbench按4xFP16一拍写入Scratchpad编写");
static_assert(fsa::ACC_SUB_BANKS==2,
              "当前testbench按4xFP32拆成两个sub-bank编写");
static_assert(fsa::nMemPorts==4,
              "当前testbench覆盖四个DMA端口的仲裁");

int failure_count = 0;

void expect(const bool condition, const std::string& message){
    if(!condition){
        std::cerr << "[FAIL] " << message << std::endl;
        ++failure_count;
    }
}

fsa::ElemVector makeElemRow(
        const float x0,
        const float x1,
        const float x2,
        const float x3){
    return {{
        (fsa::elem_t)x0,
        (fsa::elem_t)x1,
        (fsa::elem_t)x2,
        (fsa::elem_t)x3
    }};
}

fsa::AccVector makeAccRow(
        const float x0,
        const float x1,
        const float x2,
        const float x3){
    return {{
        (fsa::acc_t)x0,
        (fsa::acc_t)x1,
        (fsa::acc_t)x2,
        (fsa::acc_t)x3
    }};
}

void expectElemRow(
        const fsa::ElemVector& actual,
        const fsa::ElemVector& expected,
        const std::string& stage){
    for(int element=0; element<fsa::SA_ROWS; ++element){
        const std::size_t index = (std::size_t)element;
        if((float)actual[index]!=(float)expected[index]){
            std::cerr << "[FAIL] " << stage
                      << ", element=" << element
                      << ", actual=" << (float)actual[index]
                      << ", expected=" << (float)expected[index]
                      << std::endl;
            ++failure_count;
        }
    }
}

void expectAccRow(
        const fsa::AccVector& actual,
        const fsa::AccVector& expected,
        const std::string& stage){
    for(int element=0; element<fsa::SA_COLS; ++element){
        const std::size_t index = (std::size_t)element;
        if(actual[index]!=expected[index]){
            std::cerr << "[FAIL] " << stage
                      << ", element=" << element
                      << ", actual=" << actual[index]
                      << ", expected=" << expected[index]
                      << std::endl;
            ++failure_count;
        }
    }
}

void expectAccNarrowData(
        const fsa::AccRAMNarrowData& actual,
        const float x0,
        const float x1,
        const std::string& stage){
    expect(actual[0]==(fsa::acc_t)x0, stage + ": element 0");
    expect(actual[1]==(fsa::acc_t)x1, stage + ": element 1");
}

fsa::SpRAMTopOutput runSpRAM(const fsa::SpRAMTopInput& input){
    fsa::SpRAMTopOutput output{};
    sp_ram_top(input, output);
    return output;
}

fsa::AccRAMTopOutput runAccRAM(const fsa::AccRAMTopInput& input){
    fsa::AccRAMTopOutput output{};
    acc_ram_top(input, output);
    return output;
}

void resetSpRAM(){
    fsa::SpRAMTopInput input{};
    input.reset = true;
    const fsa::SpRAMTopOutput output = runSpRAM(input);

    expect(!output.full_read_ready, "sp reset: full read ready");
    expectElemRow(
        output.full_read_data,
        makeElemRow(0.0F, 0.0F, 0.0F, 0.0F),
        "sp reset data"
    );
}

void writeSpRow(
        const unsigned int port,
        const unsigned int address,
        const fsa::ElemVector& data){
    fsa::SpRAMTopInput input{};
    input.narrow_write_valid[port] = true;
    input.narrow_write_addr[port] = address;
    input.narrow_write_sub_bank_idx[port] = 0;
    input.narrow_write_data[port] = data;

    const fsa::SpRAMTopOutput output = runSpRAM(input);
    expect(output.narrow_write_ready[port],
           "sp write: request should be accepted");
}

fsa::ElemVector readSpRow(const unsigned int address){
    fsa::SpRAMTopInput request{};
    request.full_read_valid = true;
    request.full_read_addr = address;
    request.full_read_sub_bank_mask[0] = true;

    const fsa::SpRAMTopOutput request_output = runSpRAM(request);
    expect(request_output.full_read_ready,
           "sp read: request should be accepted");

    const fsa::SpRAMTopInput idle{};
    return runSpRAM(idle).full_read_data;
}

void testSpRAMTop(){
    resetSpRAM();

    const fsa::ElemVector row0 = makeElemRow(1.0F, 2.0F, 3.0F, 4.0F);
    const fsa::ElemVector row1 = makeElemRow(11.0F, 12.0F, 13.0F, 14.0F);
    const fsa::ElemVector row4 = makeElemRow(41.0F, 42.0F, 43.0F, 44.0F);

    /* 地址0和1落在不同物理bank，应能由两个窄写端口并行写入。 */
    fsa::SpRAMTopInput parallel_write{};
    parallel_write.narrow_write_valid[0] = true;
    parallel_write.narrow_write_addr[0] = 0;
    parallel_write.narrow_write_data[0] = row0;
    parallel_write.narrow_write_valid[1] = true;
    parallel_write.narrow_write_addr[1] = 1;
    parallel_write.narrow_write_data[1] = row1;

    fsa::SpRAMTopOutput output = runSpRAM(parallel_write);
    expect(output.narrow_write_ready[0],
           "sp parallel write: port 0 not ready");
    expect(output.narrow_write_ready[1],
           "sp parallel write: port 1 not ready");
    expectElemRow(readSpRow(0), row0, "sp parallel write row 0");
    expectElemRow(readSpRow(1), row1, "sp parallel write row 1");

    writeSpRow(0, 4, row4);

    /* 地址2和4同属bank 0；端口0优先，端口1请求不得写入。 */
    const fsa::ElemVector row2 = makeElemRow(21.0F, 22.0F, 23.0F, 24.0F);
    const fsa::ElemVector rejected =
        makeElemRow(91.0F, 92.0F, 93.0F, 94.0F);
    fsa::SpRAMTopInput conflicting_write{};
    conflicting_write.narrow_write_valid[0] = true;
    conflicting_write.narrow_write_addr[0] = 2;
    conflicting_write.narrow_write_data[0] = row2;
    conflicting_write.narrow_write_valid[1] = true;
    conflicting_write.narrow_write_addr[1] = 4;
    conflicting_write.narrow_write_data[1] = rejected;

    output = runSpRAM(conflicting_write);
    expect(output.narrow_write_ready[0],
           "sp conflict: first port should win");
    expect(!output.narrow_write_ready[1],
           "sp conflict: second port should be rejected");
    expectElemRow(readSpRow(2), row2, "sp conflict winner data");
    expectElemRow(readSpRow(4), row4, "sp conflict rejected data");

    /* 越界地址不能握手，也不能覆盖任何合法行。 */
    fsa::SpRAMTopInput invalid_write{};
    invalid_write.narrow_write_valid[0] = true;
    invalid_write.narrow_write_addr[0] = fsa::SPAD_ROWS;
    invalid_write.narrow_write_data[0] = rejected;
    output = runSpRAM(invalid_write);
    expect(!output.narrow_write_ready[0],
           "sp out-of-range write should not be ready");

    fsa::SpRAMTopInput invalid_read{};
    invalid_read.full_read_valid = true;
    invalid_read.full_read_addr = fsa::SPAD_ROWS;
    invalid_read.full_read_sub_bank_mask[0] = true;
    output = runSpRAM(invalid_read);
    expect(!output.full_read_ready,
           "sp out-of-range read should not be ready");

    /* 读请求当次事务仍返回旧响应，下一次事务才返回本次读取结果。 */
    fsa::SpRAMTopInput read_request{};
    read_request.full_read_valid = true;
    read_request.full_read_addr = 0;
    read_request.full_read_sub_bank_mask[0] = true;
    output = runSpRAM(read_request);
    expectElemRow(output.full_read_data, row4,
                  "sp one-cycle read returns previous response");

    output = runSpRAM(fsa::SpRAMTopInput{});
    expectElemRow(output.full_read_data, row0,
                  "sp one-cycle read returns requested row");
    const fsa::SpRAMTopOutput held = runSpRAM(fsa::SpRAMTopInput{});
    expectElemRow(held.full_read_data, row0,
                  "sp idle keeps read response");

    /* reset清除响应寄存器，但存储阵列内容必须保持。 */
    resetSpRAM();
    output = runSpRAM(fsa::SpRAMTopInput{});
    expectElemRow(
        output.full_read_data,
        makeElemRow(0.0F, 0.0F, 0.0F, 0.0F),
        "sp reset clears pending response"
    );
    expectElemRow(readSpRow(0), row0, "sp reset preserves memory");
}

void resetAccRAM(){
    fsa::AccRAMTopInput input{};
    input.reset = true;
    const fsa::AccRAMTopOutput output = runAccRAM(input);

    expect(!output.full_read_ready, "acc reset: full read ready");
    expect(!output.full_write_ready, "acc reset: full write ready");
    expectAccRow(
        output.full_read_data,
        makeAccRow(0.0F, 0.0F, 0.0F, 0.0F),
        "acc reset data"
    );
}

void writeAccRow(
        const unsigned int address,
        const fsa::AccVector& data,
        const bool sub_bank_0 = true,
        const bool sub_bank_1 = true){
    fsa::AccRAMTopInput input{};
    input.full_write_valid = true;
    input.full_write_addr = address;
    input.full_write_sub_bank_mask[0] = sub_bank_0;
    input.full_write_sub_bank_mask[1] = sub_bank_1;
    input.full_write_data = data;

    const fsa::AccRAMTopOutput output = runAccRAM(input);
    expect(output.full_write_ready,
           "acc write: request should be accepted");
}

fsa::AccVector readAccRow(const unsigned int address){
    fsa::AccRAMTopInput request{};
    request.full_read_valid = true;
    request.full_read_addr = address;
    request.full_read_sub_bank_mask[0] = true;
    request.full_read_sub_bank_mask[1] = true;

    const fsa::AccRAMTopOutput request_output = runAccRAM(request);
    expect(request_output.full_read_ready,
           "acc read: request should be accepted");

    return runAccRAM(fsa::AccRAMTopInput{}).full_read_data;
}

void testAccRAMTop(){
    resetAccRAM();

    const fsa::AccVector row0 = makeAccRow(1.0F, 2.0F, 3.0F, 4.0F);
    const fsa::AccVector row1 = makeAccRow(11.0F, 12.0F, 13.0F, 14.0F);
    writeAccRow(0, row0);
    writeAccRow(1, row1);
    expectAccRow(readAccRow(0), row0, "acc full write/read row 0");
    expectAccRow(readAccRow(1), row1, "acc full write/read row 1");

    /* full write mask只允许更新选中的后半行sub-bank。 */
    writeAccRow(0, makeAccRow(90.0F, 91.0F, 30.0F, 40.0F), false, true);
    const fsa::AccVector masked_row0 =
        makeAccRow(1.0F, 2.0F, 30.0F, 40.0F);
    expectAccRow(readAccRow(0), masked_row0, "acc masked full write");

    /* 同bank不同sub-bank可并行读；同bank同sub-bank按端口顺序仲裁。 */
    fsa::AccRAMTopInput narrow_read{};
    narrow_read.narrow_read_valid[0] = true;
    narrow_read.narrow_read_addr[0] = 0;
    narrow_read.narrow_read_sub_bank_idx[0] = 0;
    narrow_read.narrow_read_valid[1] = true;
    narrow_read.narrow_read_addr[1] = 0;
    narrow_read.narrow_read_sub_bank_idx[1] = 1;
    narrow_read.narrow_read_valid[2] = true;
    narrow_read.narrow_read_addr[2] = 2;
    narrow_read.narrow_read_sub_bank_idx[2] = 0;
    narrow_read.narrow_read_valid[3] = true;
    narrow_read.narrow_read_addr[3] = 1;
    narrow_read.narrow_read_sub_bank_idx[3] = 0;

    fsa::AccRAMTopOutput output = runAccRAM(narrow_read);
    expect(output.narrow_read_ready[0],
           "acc narrow arbitration: port 0 should win");
    expect(output.narrow_read_ready[1],
           "acc narrow arbitration: different sub-bank should run");
    expect(!output.narrow_read_ready[2],
           "acc narrow arbitration: later conflicting port should stop");
    expect(output.narrow_read_ready[3],
           "acc narrow arbitration: different bank should run");

    output = runAccRAM(fsa::AccRAMTopInput{});
    expectAccNarrowData(output.narrow_read_data[0], 1.0F, 2.0F,
                        "acc narrow port 0 response");
    expectAccNarrowData(output.narrow_read_data[1], 30.0F, 40.0F,
                        "acc narrow port 1 response");
    expectAccNarrowData(output.narrow_read_data[3], 11.0F, 12.0F,
                        "acc narrow port 3 response");

    /* full read优先于所有narrow read，但其他物理bank仍可并行读取。 */
    fsa::AccRAMTopInput full_priority{};
    full_priority.full_read_valid = true;
    full_priority.full_read_addr = 0;
    full_priority.full_read_sub_bank_mask[0] = true;
    full_priority.full_read_sub_bank_mask[1] = true;
    full_priority.narrow_read_valid[0] = true;
    full_priority.narrow_read_addr[0] = 0;
    full_priority.narrow_read_sub_bank_idx[0] = 0;
    full_priority.narrow_read_valid[1] = true;
    full_priority.narrow_read_addr[1] = 1;
    full_priority.narrow_read_sub_bank_idx[1] = 1;

    output = runAccRAM(full_priority);
    expect(output.full_read_ready,
           "acc full priority: full read should be ready");
    expect(!output.narrow_read_ready[0],
           "acc full priority: conflicting narrow read should stop");
    expect(output.narrow_read_ready[1],
           "acc full priority: other bank narrow read should run");

    output = runAccRAM(fsa::AccRAMTopInput{});
    expectAccRow(output.full_read_data, masked_row0,
                 "acc full priority response");
    expectAccNarrowData(output.narrow_read_data[1], 13.0F, 14.0F,
                        "acc other-bank narrow response");

    /* 读口和写口是独立端口，同拍访问同一行时读取写入前的旧值。 */
    const fsa::AccVector replacement =
        makeAccRow(101.0F, 102.0F, 103.0F, 104.0F);
    fsa::AccRAMTopInput read_and_write{};
    read_and_write.full_read_valid = true;
    read_and_write.full_read_addr = 0;
    read_and_write.full_read_sub_bank_mask[0] = true;
    read_and_write.full_read_sub_bank_mask[1] = true;
    read_and_write.full_write_valid = true;
    read_and_write.full_write_addr = 0;
    read_and_write.full_write_sub_bank_mask[0] = true;
    read_and_write.full_write_sub_bank_mask[1] = true;
    read_and_write.full_write_data = replacement;

    output = runAccRAM(read_and_write);
    expect(output.full_read_ready && output.full_write_ready,
           "acc independent read/write ports should both be ready");
    output = runAccRAM(fsa::AccRAMTopInput{});
    expectAccRow(output.full_read_data, masked_row0,
                 "acc simultaneous read observes old data");
    expectAccRow(readAccRow(0), replacement,
                 "acc simultaneous write stores new data");

    /* 所有类型的越界请求都必须被拒绝。 */
    fsa::AccRAMTopInput invalid{};
    invalid.full_read_valid = true;
    invalid.full_read_addr = fsa::ACC_ROWS;
    invalid.full_read_sub_bank_mask[0] = true;
    invalid.full_read_sub_bank_mask[1] = true;
    invalid.full_write_valid = true;
    invalid.full_write_addr = fsa::ACC_ROWS;
    invalid.full_write_sub_bank_mask[0] = true;
    invalid.full_write_sub_bank_mask[1] = true;
    invalid.narrow_read_valid[0] = true;
    invalid.narrow_read_addr[0] = fsa::ACC_ROWS;
    invalid.narrow_read_sub_bank_idx[0] = 0;

    output = runAccRAM(invalid);
    expect(!output.full_read_ready,
           "acc out-of-range full read should not be ready");
    expect(!output.full_write_ready,
           "acc out-of-range full write should not be ready");
    expect(!output.narrow_read_ready[0],
           "acc out-of-range narrow read should not be ready");

    /* reset清响应、保留SRAM内容。 */
    resetAccRAM();
    output = runAccRAM(fsa::AccRAMTopInput{});
    expectAccRow(
        output.full_read_data,
        makeAccRow(0.0F, 0.0F, 0.0F, 0.0F),
        "acc reset clears full response"
    );
    expectAccNarrowData(output.narrow_read_data[0], 0.0F, 0.0F,
                        "acc reset clears narrow response");
    expectAccRow(readAccRow(0), replacement,
                 "acc reset preserves memory");
}

}  // namespace

int main(){
#if defined(FSA_TEST_SP_RAM_TOP)
    testSpRAMTop();
#elif defined(FSA_TEST_ACC_RAM_TOP)
    testAccRAMTop();
#else
    testSpRAMTop();
    testAccRAMTop();
#endif

    if(failure_count!=0){
        std::cerr << "BankedSRAM top test failed with "
                  << failure_count << " error(s)." << std::endl;
        return 1;
    }

    std::cout << "BankedSRAM top test passed." << std::endl;
    return 0;
}
