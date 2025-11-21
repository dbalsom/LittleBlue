#ifndef XTCE_BLUE_CPU_TYPES_H
#define XTCE_BLUE_CPU_TYPES_H

enum class Register : size_t
{
    ES,
    CS,
    SS,
    DS,
    PC,
    IND,
    OPR,
    R7,
    R8,
    R9,
    R10,
    R11,
    TMPA,
    TMPB,
    TMPC,
    FLAGS,
    R16,
    R17,
    M,
    R,
    SIGMA,
    ONES,
    R22,
    R23,
    AX,
    CX,
    DX,
    BX,
    SP,
    BP,
    SI,
    DI,
};

constexpr size_t reg_to_idx(Register r) {
    return static_cast<size_t>(r);
}

enum class QueueReadState
{
    NoOperation,
    FirstByte,
    Flush,
    SubsequentByte,
};


#endif //XTCE_BLUE_CPU_TYPES_H

