#extension GL_EXT_spirv_intrinsics : require

#define FP_MODE_None 0x0
#define FP_MODE_NotNaN 0x1 //(from SPIRV docs:) Assume parameters and result are not NaN. If this assumption does not hold then the operation returns an undefined value.
#define FP_MODE_NotInf 0x2 //(from SPIRV docs:) Assume parameters and result are not +/- Inf. If this assumption does not hold then the operation returns an undefined value.
#define FP_MODE_NSZ 0x4 //(from SPIRV docs:)  Treat the sign of a zero parameter or result as insignificant.
#define FP_MODE_AllowRecip 0x8 //(from SPIRV docs:)  Allow the usage of reciprocal rather than perform a division.
#define FP_MODE_Fast 0x10 //(from SPIRV docs:)  Allow algebraic transformations according to real-number associative and distributive algebra. This flag implies all the others.

#define SPIRV_DECORATION_FPFastMathMode 40

#define SET_FP_MODE_ON_FOLLOWING_STATEMENT(fp_mode_flag) spirv_decorate(SPIRV_DECORATION_FPFastMathMode, fp_mode_flag)