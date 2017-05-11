/*---------------------------------------------------------------------
*
* Copyright © 2015  Minsi Chen
* E-mail: m.chen@derby.ac.uk
*
* The source is written for the Graphics I and II modules. You are free
* to use and extend the functionality. The code provided here is functional
* however the author does not guarantee its performance.
---------------------------------------------------------------------*/
#pragma once

#include "Material.h"
#include "Ray.h"
#include "Scene.h"
#include "Framebuffer.h"

class PathTracer
{
private:
	Framebuffer		*m_framebuffer;
	int				m_buffWidth;
	int				m_buffHeight;
	int				m_renderCount;
	int				m_traceLevel;
	int				m_spp = 1;

	//Main function that returns the radiance
	Colour Radiance(Scene* pScene, Ray& ray, int tracelevel);

	//Random vector in hemisphere
	Vector3 SampleDiffuse(Vector3 normal);

public:

	enum TraceFlags
	{
		NO_TRACE = 0,							//disables path tracing so that ray tracing may be used
		TRACE_DIFFUSE = 0x1,					//trace diffuse colour only
		TRACE_GLOSS = 0x1 << 1,					//trace and compute glossy materials 
	};

	TraceFlags m_traceflag;						//current trace flags value default is TRACE_AMBIENT

	PathTracer();
	PathTracer(int widht, int height, int spp);
	PathTracer(int width, int height);
	~PathTracer();

	inline void SetSPP(int spp)
	{
		m_spp = spp;
	}

	inline void SetTraceLevel(int level)		//Set the level of recursion, default is 5
	{
		m_traceLevel = level;
	}

	inline void ResetRenderCount()
	{
		m_renderCount = 0;
	}

	inline Framebuffer *GetFramebuffer() const
	{
		return m_framebuffer;
	}

	//Generate a random vector in a hemisphere.

	inline double GetUniformDouble()
	{
		return (double)rand() / (double)RAND_MAX;
	}

	inline double GetMax(Vector3 colour)
	{
		double x = colour[0];
		double y = colour[1];
		double z = colour[2];

		if (x > y && x > z)
		{
			return x;
		}
		else
		{
			if (y > z)
			{
				return y;
			}
			else
			{
				return z;
			}
		}
	}

	//Trace a given scene
	//Params: Scene* pScene   Pointer to the scene to be ray traced
	void DoRayTrace(Scene* pScene);
};

