/* 
 * Copyright (c) 2013 Opposite Renderer
 * For the full copyright and license information, please view the LICENSE.txt
 * file that was distributed with this source code.
 */

#include <optix.h>
#include <optix_cuda.h>
#include <optixu/optixu_math_namespace.h>
#include "config.h"
#include "renderer/Hitpoint.h"
#include "renderer/RayType.h"
#include "renderer/RadiancePRD.h"
#include "renderer/ppm/PhotonPRD.h"
#include "renderer/ppm/Photon.h"
#include "renderer/helpers/random.h"
#include "renderer/helpers/helpers.h"
#include "renderer/helpers/samplers.h"
#include "renderer/helpers/store_photon.h"
#include "renderer/vcm/SubpathPRD.h"
#include "renderer/vcm/PathVertex.h"

using namespace optix;

rtDeclareVariable(uint2, launchDim, rtLaunchDim, );
rtDeclareVariable(uint2, launchIndex, rtLaunchIndex, );
rtDeclareVariable(RadiancePRD, radiancePrd, rtPayload, );
rtDeclareVariable(PhotonPRD, photonPrd, rtPayload, );
rtDeclareVariable(optix::Ray, ray, rtCurrentRay, );
rtDeclareVariable(float, tHit, rtIntersectionDistance, );

rtDeclareVariable(float3, geometricNormal, attribute geometricNormal, ); 
rtDeclareVariable(float3, shadingNormal, attribute shadingNormal, ); 

rtBuffer<Photon, 1> photons;
rtBuffer<Hitpoint, 2> raytracePassOutputBuffer;
rtDeclareVariable(rtObject, sceneRootObject, , );
rtDeclareVariable(uint, maxPhotonDepositsPerEmitted, , );
rtDeclareVariable(float3, Kd, , );

#if ACCELERATION_STRUCTURE == ACCELERATION_STRUCTURE_STOCHASTIC_HASH
rtDeclareVariable(uint3, photonsGridSize, , );
rtDeclareVariable(float3, photonsWorldOrigo, ,);
rtDeclareVariable(float, photonsGridCellSize, ,);
rtDeclareVariable(unsigned int, photonsSize,,);
rtBuffer<unsigned int, 1> photonsHashTableCount;
#endif


/*
// Radiance Program
*/
RT_PROGRAM void closestHitRadiance()
{
    float3 worldShadingNormal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shadingNormal ) );
    float3 hitPoint = ray.origin + tHit*ray.direction;

    radiancePrd.flags |= PRD_HIT_NON_SPECULAR;
    radiancePrd.attenuation *= Kd;
    radiancePrd.normal = worldShadingNormal;
    radiancePrd.position = hitPoint;
    radiancePrd.lastTHit = tHit;
    radiancePrd.depth++; // vmarz: using for debugging (was already defined in struct)
    if(radiancePrd.flags & PRD_PATH_TRACING)
    {
        float2 sample = getRandomUniformFloat2(&radiancePrd.randomState);
        radiancePrd.randomNewDirection = sampleUnitHemisphereCos(worldShadingNormal, sample);
    }
}

/*
// Photon Program
*/
RT_PROGRAM void closestHitPhoton()
{
    float3 worldShadingNormal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shadingNormal ) );
    float3 hitPoint = ray.origin + tHit*ray.direction;
    float3 newPhotonDirection;

    if(photonPrd.depth >= 1 && photonPrd.numStoredPhotons < maxPhotonDepositsPerEmitted)
    {
        Photon photon (photonPrd.power, hitPoint, ray.direction, worldShadingNormal);
        STORE_PHOTON(photon);
    }

    photonPrd.power *= Kd;
    OPTIX_DEBUG_PRINT(photonPrd.depth, "Hit Diffuse P(%.2f %.2f %.2f) RT=%d\n", hitPoint.x, hitPoint.y, hitPoint.z, ray.ray_type);
    photonPrd.weight *= fmaxf(Kd);

    // Use russian roulette sampling from depth X to limit the length of the path

    if( photonPrd.depth >= PHOTON_TRACING_RR_START_DEPTH)
    {
        float probContinue = favgf(Kd);
        float probSample = getRandomUniformFloat(&photonPrd.randomState);
        if(probSample >= probContinue )
        {
            return;
        }
        photonPrd.power /= probContinue;
    }

    photonPrd.depth++;
    if(photonPrd.depth >= MAX_PHOTON_TRACE_DEPTH || photonPrd.weight < 0.001)
    {
        return;
    }

#if ACCELERATION_STRUCTURE == ACCELERATION_STRUCTURE_UNIFORM_GRID || ACCELERATION_STRUCTURE == ACCELERATION_STRUCTURE_KD_TREE_CPU
    if(photonPrd.numStoredPhotons >= maxPhotonDepositsPerEmitted)
        return;
#endif

    newPhotonDirection = sampleUnitHemisphereCos(worldShadingNormal, getRandomUniformFloat2(&photonPrd.randomState));
    optix::Ray newRay( hitPoint, newPhotonDirection, RayType::PHOTON, 0.0001 );
    rtTrace(sceneRootObject, newRay, photonPrd);
}


rtDeclareVariable(SubpathPRD, lightPrd, rtPayload, );
rtBuffer<ushort, 2> lightVertexCountBuffer;


 // Light subpath program
RT_PROGRAM void closestHitLight()
{
    printf("  Diffuse hit idx %d, %d \n", launchIndex.x, launchIndex.y);
    lightPrd.done = 1;
    return;

    if ( launchIndex.x + launchIndex.y == 0 )
        printf("  Diffuse hit id %d %d dep %d\n", launchIndex.x, launchIndex.y, lightPrd.depth);
    //else
    //{  
    //    lightPrd.done = 1;
    //    return;
    //}

	// vmarz TODO make sure shading normals used correctly
    float3 worldShadingNormal = normalize( rtTransformNormal( RT_OBJECT_TO_WORLD, shadingNormal ) );
    float3 hitPoint = ray.origin + tHit*ray.direction;

    if ( launchIndex.x + launchIndex.y == 0 )
    {
        printf("  Hit point %f %f %f\n", hitPoint.x, hitPoint.y, hitPoint.z);
        printf("  Hit normal %f %f %f\n", worldShadingNormal.x, worldShadingNormal.y, worldShadingNormal.z);
    }
	// Update MIS quantities before storing at the vertex

	// vmarz TODO infinite lights need attitional handling
	float hitCosTheta = dot(worldShadingNormal, -ray.direction);
	if (hitCosTheta < 0) return;	// vmarz TODO check validity

    if ( launchIndex.x + launchIndex.y == 0 )
    {
        printf("  Cos theta %f \n", hitCosTheta);
    }

	float misFactor = 1.0f / Mis(fabs(hitCosTheta));
	lightPrd.dVCM *= misFactor;  // vmarz?: need abs here?
	lightPrd.dVC *= misFactor;
	lightPrd.dVM *= misFactor;
	lightPrd.depth++;

	PathVertex lightVertex;
	lightVertex.hitPoint = hitPoint;
	lightVertex.throughput = lightPrd.throughput;
	lightVertex.pathDepth = lightPrd.depth;
	lightVertex.dVCM = lightPrd.dVCM;
	lightVertex.dVC = lightPrd.dVC;
	lightVertex.dVM = lightPrd.dVM;
	// vmarz TODO store material bsdf

	// store path vertex
	lightVertexCountBuffer[launchIndex] = lightPrd.depth;

	// vmarz TODO connect to camera
	// vmarz TODO check max path length
	
	// Russian Roulette
	float contProb = luminanceCIE(Kd); // vmarz TODO precompute
	float rrSample = getRandomUniformFloat(&lightPrd.randomState);
    if ( launchIndex.x + launchIndex.y == 0 )
        printf("  Cont %f RR %f \n", contProb, rrSample);

	if (contProb < rrSample)
	{
        if ( launchIndex.x + launchIndex.y == 0 )
            printf("  Diff %d,%d - Dep %d - done\n", launchIndex.x, launchIndex.y, lightPrd.depth);
		lightPrd.done = 1;
		return;
	}

	// next event
	// vmarz TODO MIS initialized during light emission or new dir 
	// bsdf sample, vmarz TODO handle multiple bsdf components
	float3 bsdfFactor = Kd * M_1_PIf;
	float bsdfDirPdfW;
	float cosThetaOut;
	float2 bsdfSample = getRandomUniformFloat2(&lightPrd.randomState);
	lightPrd.direction = sampleUnitHemisphereCos(worldShadingNormal, bsdfSample, &bsdfDirPdfW, &cosThetaOut);

    if ( launchIndex.x + launchIndex.y == 0 )
    {
        printf("  New dir %f %f %f\n", lightPrd.direction.x, lightPrd.direction.y, lightPrd.direction.z);
    }

	float bsdfRevPdfW; // vmarz TODO
	// check component cont prob	
	// vmarz TODO MIS weights VCM 990

	bsdfDirPdfW *= contProb;
	bsdfRevPdfW *= contProb;

	lightPrd.throughput *= bsdfFactor * (cosThetaOut / bsdfDirPdfW);
	lightPrd.origin = hitPoint;
}

//struct PathVertex
//{
//	optix::float3 hitPoint;
//  optix::float3 throughput;
//  float pathDepth;
//	float dVCM;
//	float dVC;
//	float dVM;
//	// bsdf data
//	optix::uint materialID;
//};

//struct SubpathPRD
//{
//  optix::float3 origin;
//	optix::float3 direction;
//	optix::float3 throughput;
//  optix::uint depth;
//  RandomState randomState;
//	float dVCM;
//	float dVC;
//	float dVM;
//	//uint  mIsFiniteLight :  1; // Just generate by finite light
//  //uint  mSpecularPath  :  1; // All scattering events so far were specular
//};