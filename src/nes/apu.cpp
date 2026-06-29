#include <platform-nes/apu.hpp>

#ifdef TARGET_NES

wo_register<0x4000> apu::sq1_vol;
wo_register<0x4001> apu::sq1_sweep;
wo_register<0x4002> apu::sq1_lo;
wo_register<0x4003> apu::sq1_hi;
wo_register<0x4004> apu::sq2_vol;
wo_register<0x4005> apu::sq2_sweep;
wo_register<0x4006> apu::sq2_lo;
wo_register<0x4007> apu::sq2_hi;
wo_register<0x4008> apu::tri_linear;
wo_register<0x400A> apu::tri_lo;
wo_register<0x400B> apu::tri_hi;
wo_register<0x400C> apu::noise_vol;
wo_register<0x400E> apu::noise_lo;
wo_register<0x400F> apu::noise_hi;
wo_register<0x4010> apu::dmc_freq;
wo_register<0x4011> apu::dmc_raw;
wo_register<0x4012> apu::dmc_start;
wo_register<0x4013> apu::dmc_len;
wo_register<0x4015> apu::snd_chn;
wo_register<0x4017> apu::frame_counter;

u8 APU_REGISTERS_snapshot[20];

#endif
