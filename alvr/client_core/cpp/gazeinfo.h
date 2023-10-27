#pragma once


#include <vector>
#include <string>


struct GazeCenterInfo
{
    float x;
    float y;
};

struct OriAngle
{
   float head_x = 0;
   float head_y = 0;
   float gaze_x = 0;
   float gaze_y = 0;
};


struct GaussianKernel3
{
   float a ;

   float center ;
};

struct GaussianKernel5
{
   float a ;
   float b ;

   float center ;
};

struct GaussianKernel7
{
   float a ;
   float b ;
   float c ;

   float center ;

};

