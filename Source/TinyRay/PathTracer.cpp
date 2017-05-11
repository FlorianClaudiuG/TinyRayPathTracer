/*---------------------------------------------------------------------
*
* Copyright © 2015  Minsi Chen
* E-mail: m.chen@derby.ac.uk
*
* The source is written for the Graphics I and II modules. You are free
* to use and extend the functionality. The code provided here is functional
* however the author does not guarantee its performance.
---------------------------------------------------------------------*/
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <iostream>
#include <iomanip>


#if defined(WIN32) || defined(_WINDOWS)
#include <Windows.h>
#include <gl/GL.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "PathTracer.h"
#include "Ray.h"
#include "Scene.h"
#include "Camera.h"
#include "perlin.h"

#define M_PI 3.14159265358979323846

PathTracer::PathTracer()
{
	m_buffHeight = m_buffWidth = 0.0;
	m_renderCount = 0;
	SetSPP(500);
	SetTraceLevel(5);
	m_traceflag = (TraceFlags)(TRACE_DIFFUSE | TRACE_GLOSS);

}

PathTracer::PathTracer(int Width, int Height)
{
	m_buffWidth = Width;
	m_buffHeight = Height;
	m_renderCount = 0;
	SetTraceLevel(5);
	srand(time(NULL));

	m_framebuffer = new Framebuffer(Width, Height);

	//default set default trace flag, i.e. no lighting, non-recursive
	m_traceflag = (TraceFlags)(TRACE_DIFFUSE);
}

PathTracer::PathTracer(int Width, int Height, int Spp)
{
	m_buffWidth = Width;
	m_buffHeight = Height;
	m_renderCount = 0;
	SetSPP(100);
	SetTraceLevel(5);
	srand(time(NULL));

	m_framebuffer = new Framebuffer(Width, Height);

	//default set default trace flag, i.e. no lighting, non-recursive
	m_traceflag = (TraceFlags)(TRACE_DIFFUSE);
}

PathTracer::~PathTracer()
{
	delete m_framebuffer;
}

void PathTracer::DoRayTrace(Scene* pScene)
{
	Camera* cam = pScene->GetSceneCamera();

	Vector3 camRightVector = cam->GetRightVector();
	Vector3 camUpVector = cam->GetUpVector();
	Vector3 camViewVector = cam->GetViewVector();
	Vector3 centre = cam->GetViewCentre();
	Vector3 camPosition = cam->GetPosition();

	double sceneWidth = pScene->GetSceneWidth();
	double sceneHeight = pScene->GetSceneHeight();

	double pixelDX = sceneWidth / m_buffWidth;
	double pixelDY = sceneHeight / m_buffHeight;

	int total = m_buffHeight*m_buffWidth;
	int done_count = 0;

	Vector3 start;

	start[0] = centre[0] - ((sceneWidth * camRightVector[0])
		+ (sceneHeight * camUpVector[0])) / 2.0;
	start[1] = centre[1] - ((sceneWidth * camRightVector[1])
		+ (sceneHeight * camUpVector[1])) / 2.0;
	start[2] = centre[2] - ((sceneWidth * camRightVector[2])
		+ (sceneHeight * camUpVector[2])) / 2.0;

	Colour scenebg = pScene->GetBackgroundColour();

	if (m_renderCount == 0)
	{
		fprintf(stdout, "Trace start.\n");
		clock_t begin = clock();
		Colour colour;
		double oldpercent = 0;
		//TinyRay on multiprocessors using OpenMP!!!
#pragma omp parallel for schedule (dynamic, 8) private(colour)
		for (int i = 0; i < m_buffHeight; i += 1)
		{
			for (int j = 0; j < m_buffWidth; j += 1)
			{
				//calculate the metric size of a pixel in the view plane (e.g. framebuffer)
				Vector3 pixel;

				pixel[0] = start[0] + (i + 0.5) * camUpVector[0] * pixelDY
					+ (j + 0.5) * camRightVector[0] * pixelDX;
				pixel[1] = start[1] + (i + 0.5) * camUpVector[1] * pixelDY
					+ (j + 0.5) * camRightVector[1] * pixelDX;
				pixel[2] = start[2] + (i + 0.5) * camUpVector[2] * pixelDY
					+ (j + 0.5) * camRightVector[2] * pixelDX;

				/*
				* setup first generation view ray
				* In perspective projection, each view ray originates from the eye (camera) position
				* and pierces through a pixel in the view plane
				*/
				Ray viewray;
				viewray.SetRay(camPosition, (pixel - camPosition).Normalise());

				double u = (double)j / (double)m_buffWidth;
				double v = (double)i / (double)m_buffHeight;

				colour = this->Radiance(pScene, viewray, m_traceLevel);

				for (int sample = 0; sample < m_spp; sample++)
				{
					colour = colour + this->Radiance(pScene, viewray,  m_traceLevel);
				}
				colour = colour * (1./ m_spp);
				m_framebuffer->WriteRGBToFramebuffer(colour, j, i);
			}
			double percent = double(i) / double(m_buffHeight);
			printf("%.2f\%%\r", percent*100);
		}
		clock_t end = clock();
		double elapsedseconds = double(end - begin);		//Count CPU ticks.

		fprintf(stdout, "Finished in %f CPU ticks!\n", elapsedseconds);
		m_renderCount++;
	}
}

Colour PathTracer::Radiance(Scene* pScene, Ray& ray, int tracelevel)
{
	//Colour bg = pScene->GetBackgroundColour();
	Colour outcolour;

	RayHitResult result;
	result = pScene->IntersectByRay(ray);
	
	if (result.data)
	{
		Primitive* prim = (Primitive*)result.data;
		Material* mat = prim->GetMaterial();

		Colour emission = mat->GetEmissiveColour();
		Colour specular = mat->GetSpecularColour();
		Colour diffuse = mat->GetDiffuseColour();

		Vector3 intersection = result.point;
		Vector3 normal = result.normal;

		double reflectance = GetMax(diffuse);

		if (--tracelevel < 0)
		{
			if (GetUniformDouble() < reflectance)
			{
				diffuse = diffuse * (1. / reflectance);
			}
			else
			{
				return emission;
			}
		}

		Ray diffuseRay;
		Ray glossRay;

		Vector3 newDirection = SampleDiffuse(normal);
		diffuseRay.SetRay(intersection + newDirection * 0.0001f, newDirection);

		if (m_traceflag & TRACE_GLOSS)
		{
			if (((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Box || ((Primitive*)result.data)->m_primtype == Primitive::PRIMTYPE_Sphere)
			{
				Vector3 reflection = (ray.GetRay().Reflect(normal)).Normalise();
				glossRay.SetRay(intersection + reflection * 0.0001f, reflection);
				outcolour = emission + diffuse * Radiance(pScene, glossRay, tracelevel);
				return outcolour;
			}
			else
			{
				outcolour = emission + diffuse * Radiance(pScene, diffuseRay, tracelevel);
				return outcolour;
			}
		}

		outcolour = emission + diffuse * Radiance(pScene, diffuseRay, tracelevel);
	}
	
	return outcolour;

}

Vector3 PathTracer::SampleDiffuse(Vector3 normal)
{
	//Get the random radii for the vector.
	double r1 = 2 * M_PI * GetUniformDouble();
	double r2 = GetUniformDouble();
	double r2s = sqrt(r2);

	Vector3 u = ((fabs(normal[0]) > .1 ? Vector3(0, 1, 0) : Vector3(1, 0, 0)).CrossProduct(normal)).Normalise();
	Vector3 v = normal.CrossProduct(u);
	Vector3 d = (u*cos(r1)*r2s + v*sin(r1)*r2s + normal*sqrt(1 - r2)).Normalise();

	return d;
}

