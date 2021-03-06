/* 
 * Copyright (c) 2014 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 *
 * Contributions: Stian Pedersen
 *                Valdis Vilcans
*/

#pragma once
#include <optixu_vector_types.h>
#include "renderer/helpers/helpers.h"

#ifdef __CUDACC__
#define _VECTOR3_DEVICE_CODE_ __device__ __host__
#else
#define _VECTOR3_DEVICE_CODE_ 
#endif

class Vector3
{
public:
    _VECTOR3_DEVICE_CODE_ Vector3()
    {

    }
    _VECTOR3_DEVICE_CODE_ Vector3(const optix::float3 & f3)
    {
        __of3 = f3;
    }
    _VECTOR3_DEVICE_CODE_ Vector3(float a)
    {
        this->x = a;
        this->y = a;
        this->z = a;
    }
    _VECTOR3_DEVICE_CODE_ Vector3(float x, float y, float z)
    {
        this->x = x;
        this->y = y;
        this->z = z;
    }

    _VECTOR3_DEVICE_CODE_ operator optix::float3 () const
    {
        return __of3;
    }

#ifndef __CUDACC__
    Vector3 operator + (const Vector3 & other) const;
    Vector3 operator - (const Vector3 & other) const;
    Vector3 operator * (float) const;
    float length();
    float length2();
#endif

    union 
    {
        struct {float x, y, z;};
        optix::float3 __of3;
    };
    static float dot(const Vector3 & a, const Vector3 & b);
    float max() { return maxf(maxf(x,y),z); }
};

__inline__ Vector3 operator += (Vector3 & a, float b)
{
    a.x += b;
    a.y += b;
    a.z += b;
    return a;
}

__inline__ Vector3 operator -= (Vector3 & a, float b)
{
    a += -b;
    return a;
}