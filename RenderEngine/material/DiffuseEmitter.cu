/* 
 * Copyright (c) 2013 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 */

#include <optix.h>
#include <optix_device.h>
#include <optixu/optixu_math_namespace.h>
#include "renderer/RadiancePRD.h"
#include "renderer/ShadowPRD.h"
#include "renderer/ppm/PhotonPRD.h"
#include "renderer/vcm/SubpathPRD.h"

using namespace optix;

rtDeclareVariable(float3, shadingNormal, attribute shadingNormal, ); 
rtDeclareVariable(optix::Ray, ray, rtCurrentRay, );
rtDeclareVariable(float, tHit, rtIntersectionDistance, );
rtDeclareVariable(RadiancePRD, radiancePrd, rtPayload, );
rtDeclareVariable(float3, powerPerArea, , );
rtDeclareVariable(float3, Kd, , );
rtDeclareVariable(ShadowPRD, shadowPrd, rtPayload, );
rtDeclareVariable(PhotonPRD, photonPrd, rtPayload, );

/*
// Radiance Program
*/
RT_PROGRAM void closestHitRadiance()
{
    float3 worldShadingNormal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shadingNormal ) );
    float3 Le = powerPerArea/M_PIf;
    radiancePrd.radiance += radiancePrd.attenuation*Le;
    radiancePrd.flags |= PRD_HIT_EMITTER;
    radiancePrd.lastTHit = tHit;
}

/*
// Photon Program
*/
RT_PROGRAM void closestHitPhoton()
{
   photonPrd.depth++;
}


// Radiance shadow program
RT_PROGRAM void gatherAnyHitOnEmitter()
{
    shadowPrd.attenuation = 1.0f;
    rtTerminateRay();
}



rtDeclareVariable(SubpathPRD, subpathPrd, rtPayload, );

/*
// VCM Light Program
*/
RT_PROGRAM void closestHitLight()
{
    subpathPrd.done = 1;
}


/*
// VCM Camera Program
*/
RT_PROGRAM void vcmClosestHitCamera()
{
    subpathPrd.done = 1;
}