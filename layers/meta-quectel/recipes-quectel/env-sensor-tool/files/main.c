#include <stdio.h>
#include <lgpio.h>

#define I2C_DEV_NUM 9
#define BME280_ADDR 0x76

int32_t digT[3],digP[9],digH[6];
int32_t t_fine = 0.0;

double compensate_P(int32_t adc_P)
{
    double pressure = 0.0;
    double v1,v2;
    v1 = (t_fine / 2.0) - 64000.0;
    v2 = (((v1 / 4.0) * (v1 / 4.0)) / 2048) * digP[5];
    v2 = v2 + ((v1 * digP[4]) * 2.0);
    v2 = (v2 / 4.0) + (digP[3] * 65536.0);
    v1 = (((digP[2] * (((v1 / 4.0) * (v1 / 4.0)) / 8192)) / 8)  + ((digP[1] * v1) / 2.0)) / 262144;
    v1 = ((32768 + v1) * digP[0]) / 32768;
    if(v1 == 0)
        return 0;
    pressure = ((1048576 - adc_P) - (v2 / 4096)) * 3125;
    if (pressure < 0x80000000)
        pressure = (pressure * 2.0) / v1;
    else
        pressure = (pressure / v1) * 2;
    v1 = (digP[8] * (((pressure / 8.0) * (pressure / 8.0)) / 8192.0)) / 4096;
    v2 = ((pressure / 4.0) * digP[7]) / 8192.0;
    pressure = pressure + ((v1 + v2 + digP[6]) / 16.0) ;
    return (pressure/100);
}

double compensate_T(int32_t adc_T)
{
    double temperature = 0.0;
    double v1,v2;
    v1 = (adc_T / 16384.0 - digT[0] / 1024.0) * digT[1];
    v2 = (adc_T / 131072.0 - digT[0] / 8192.0) * (adc_T / 131072.0 - digT[0] / 8192.0) * digT[2];
    t_fine = v1 + v2;
    temperature = t_fine / 5120.0;
    return temperature;
}

double compensate_H(int32_t adc_H)
{       
    double var_h = t_fine - 76800.0;
    if (var_h == 0)
        return 0;
    var_h = (adc_H - (digH[3] * 64.0 + digH[4]/16384.0 * var_h)) *
        (digH[1] / 65536.0 * (1.0 + digH[5] / 67108864.0 * var_h * (1.0 + digH[2] / 67108864.0 * var_h)));
    var_h = var_h * (1.0 - digH[0] * var_h / 524288.0);
    if (var_h > 100.0)
        var_h = 100.0;
    else if (var_h < 0.0)
        var_h = 0.0;
    return var_h;
} 

void get_calib_param(int handle)
{
    uint8_t calib[32];
    for(int i=0;i<24;i++) {   
        calib[i] = lgI2cReadByteData(handle, 0x88 + i); 
    }   
    calib[24] = lgI2cReadByteData(handle, 0xA1);
    for(int i=25,o=0;i<32;i++,o++) {   
        calib[i] = lgI2cReadByteData(handle, 0xE1 + o); 
    }   
    digT[0] = (calib[1] << 8) | calib[0];
    digT[1] = (calib[3] << 8) | calib[2];
    digT[2] = (calib[5] << 8) | calib[4];
    digP[0] = (calib[7] << 8) | calib[6];
    digP[1] = (calib[9] << 8) | calib[8];
    digP[2] = (calib[11] << 8) | calib[10];
    digP[3] = (calib[13] << 8) | calib[12];
    digP[4] = (calib[15] << 8) | calib[14];
    digP[5] = (calib[17] << 8) | calib[16];
    digP[6] = (calib[19] << 8) | calib[18];
    digP[7] = (calib[21] << 8) | calib[20];
    digP[8] = (calib[23] << 8) | calib[22];
    digH[0] = calib[24];
    digH[1] = (calib[26] << 8) | calib[25];
    digH[2] = calib[27];
    digH[3] = (calib[28] << 4) | (0x0f & calib[29]);
    digH[4] = (calib[30] << 4) | ((calib[29] >> 4) & 0x0f);
    digH[5] = calib[31];
    for(int i=1;i<2;i++)
        if((digT[i] & 0x8000) != 0) digT[i] = (-digT[i] ^ 0xFFFF) + 1;
    for(int i=1;i<8;i++)        
        if ((digP[i] & 0x8000) != 0)    digP[i]=(-digP[i] ^ 0xFFFF) + 1 ;    
    for(int i=0;i<6;i++)    
        if ((digH[i] & 0x8000) != 0) digH[i] = (-digH[i] ^ 0xFFFF) + 1;
}

int main(int argc, char **argv)
{
    uint8_t data[8];
    double value[3];
    int handle = lgI2cOpen(I2C_DEV_NUM, BME280_ADDR, 0);

    lgI2cWriteByteData(handle, 0xF2, 0x01);
    lgI2cWriteByteData(handle, 0xF4, 0x27);
    lgI2cWriteByteData(handle, 0xF5, 0xA0);
    get_calib_param(handle);

    for(int i=0;i<8;i++) {
        data[i] = lgI2cReadByteData(handle, 0xF7 + i); 
    }
    value[0] = (data[0] << 12) | (data[1] << 4) | (data[2] >> 4); 
    value[1] = (data[3] << 12) | (data[4] << 4) | (data[5] >> 4); 
    value[2] = (data[6] << 8)  |  data[7];
    value[0] = compensate_P(value[0]);
    value[1] = compensate_T(value[1]);
    value[2] = compensate_H(value[2]);

    printf("pressure:    %7.2f hPa\n", value[0]);
    printf("temperature: %7.2f C\n"  , value[1]);
    printf("humidity:    %7.2f %\n"  , value[2]);
    lgI2cClose(handle);
}

