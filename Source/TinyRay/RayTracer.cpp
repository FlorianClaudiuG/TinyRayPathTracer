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


#if defined(WIN32) || defined(_WINDOWS)
#include <Windows.h>
#include <gl/GL.h>
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#endif

#include "RayTracer.h"
#include "Ray.h"
#include "Scene.h"
#include "Camera.h"
#include "perlin.h"

RayTracer::RayTracer()
{
	m_buffHeight = m_buffWidth = 0.0;
	m_renderCount = 0;
	SetTraceLevel(5);
	m_traceflag = (TraceFlags)(TRACE_AMBIENT | TRACE_DIFFUSE_AND_SPEC |
		TRACE_SHADOW | TRACE_REFLECTION | TRACE_REFRACTION);

}

RayTracer::RayTracer(int Width, int Height)
{
	m_buffWidth = Width;
	m_buffHeight = Height;
	m_renderCount = 0;
	SetTraceLevel(5);

	m_framebuffer = new Framebuffer(Width, Height);

	//default set default trace flag, i.e. no lighting, non-recursive
	m_traceflag = (TraceFlags)(TRACE_AMBIENT);
}

RayTracer::~RayTracer()
{
	delete m_framebuffer;
}

void RayTracer::DoRayTrace( Scene* pScene )
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

		Colour colour;
//TinyRay on multiprocessors using OpenMP!!!
#pragma omp parallel for schedule (dynamic, 1) private(colour)
		for (int i = 0; i < m_buffHeight; i+=1) {
			for (int j = 0; j < m_buffWidth; j+=1) {

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
				//viewray.SetRay(camPosition, ( camPosition-pixel).Normalise());
				
				double u = (double)j / (double)m_buffWidth;
				double v = (double)i / (double)m_buffHeight;

				scenebg = pScene->GetBackgroundColour();

				//trace the scene using the view ray
				//default colour is the background colour, unless something is hit along the way
				colour = this->TraceScene(pScene, viewray, scenebg, m_traceLevel);

				/*
				* Draw the pixel as a coloured rectangle
				*/
				m_framebuffer->WriteRGBToFramebuffer(colour, j, i);
			}
		}

		fprintf(stdout, "Done!!!\n");
		m_renderCount++;
	}
}

Colour RayTracer::TraceScene(Scene* pScene, Ray& ray, Colour incolour, int tracelevel, bool shadowray)
{
	RayHitResult result;

	Colour outcolour = incolour; //the output colour based on the ray-primitive intersection
	
	std::vector<Light*> *light_list = pScene->GetLightList();
	std::vector<Light*>::iterator lit_iter = light_list->begin();
	Vector3 cameraPosition = pScene->GetSceneCamera()->GetPosition();

	//Intersect the ray with the scene
	//TODO: Scene::IntersectByRay needs to be implemented first
	result = pScene->IntersectByRay(ray);

	if (result.data) //the ray has hit something
	{
		outcolour = CalculateLighting(light_list, &cameraPosition, &result);

		if (m_traceflag & TRACE_REFLECTION)
		{
			if (((Primitive*)result.data)->m_primtype != Primitive::PRIMTYPE_Plane)
			{
				const double err = 1e-5;
				ray.SetRay(result.point + result.normal * err, ray.GetRay().Reflect(result.normal));
				//For recursivity.
				if (tracelevel != 0) 
				{
					tracelevel--;
					outcolour = outcolour * TraceScene(pScene, ray, outcolour, tracelevel, shadowray);
				}
			}
		}

		if (m_traceflag & TRACE_REFRACTION)
		{
			if (((Primitive*)result.data)->m_primtype != Primitive::PRIMTYPE_Plane)
			{
				//Offset for refraction ray.
				const double err = 4.0001;
				//Arbitrary refraction index.
				double index = 1.5;

				double coefficient = ray.GetRay().DotProduct(result.normal);
				if (coefficient < 0)
				{
					coefficient = 0;
				}
				if (coefficient > 1)
				{
					coefficient = 1;
				}

				//Formula to determine the r_index of the refract method.
				double ref = coefficient / index;

				ray.SetRay(result.point - result.normal * err, (ray.GetRay()).Refract(result.normal, ref));
				
				if (tracelevel != 0)
				{
					tracelevel--;
					//outcolour is multiplied by 0.4 because otherwise the scene would be too bright.
					outcolour = outcolour * 0.4 + TraceScene(pScene, ray, outcolour, tracelevel, shadowray);
				}
			}
		}

		if (m_traceflag & TRACE_SHADOW)
		{
			bool drawshadow = true;	

			while (lit_iter != light_list->end())
			{
				Vector3 lightpos = (*lit_iter)->GetLightPosition();
				Ray shadow_ray;
				const double offset = 1e-5;
				shadow_ray.SetRay(result.point + result.normal * offset, (lightpos - result.point).Normalise());
				RayHitResult shadowhit = pScene->IntersectByRay(shadow_ray);	

				//We're only interested in shadows that form in front of the camera. (result.point[2] > cameraPosition[2]) fixes the upside-down shadows that form on the ceiling.
				if (!(((Primitive*)shadowhit.data)->m_primtype != Primitive::PRIMTYPE_Plane) || (result.point[2] > cameraPosition[2]))
				{
					drawshadow = false;
				}
				lit_iter++;
			}
			if (drawshadow)
			{
				outcolour = outcolour * 0.2;
			}
		}
	}
		
	return outcolour;
}

Colour RayTracer::CalculateLighting(std::vector<Light*>* lights, Vector3* campos, RayHitResult* hitresult)
{
	Colour outcolour;
	std::vector<Light*>::iterator lit_iter = lights->begin();

	Primitive* prim = (Primitive*)hitresult->data;
	Material* mat = prim->GetMaterial();

	outcolour = mat->GetAmbientColour();
	
	//Generate the grid pattern on the plane
	if (((Primitive*)hitresult->data)->m_primtype == Primitive::PRIMTYPE_Plane)
	{
		int dx = hitresult->point[0]/2.0;
		int dy = hitresult->point[1]/2.0;
		int dz = hitresult->point[2]/2.0;

		if (dx % 2 || dy % 2 || dz % 2 )
		{
			outcolour = outcolour + Vector3(0.1, 0.1, 0.1);
		}
		else
		{
			outcolour = mat->GetDiffuseColour();
		}
		return outcolour;
	}

	////Go through all lights in the scene
	////Note the default scene only has one light source
	if (m_traceflag & TRACE_DIFFUSE_AND_SPEC)
	{
		while (lit_iter != lights->end())
		{
			Vector3 endpoint = hitresult->point;							
			Vector3 n = hitresult->normal;
			Vector3 lightposition = (*lit_iter)->GetLightPosition();
			Vector3 l = (lightposition - endpoint).Normalise();
			Vector3 e = (*campos - endpoint).Normalise();
			Vector3 i = (endpoint - lightposition).Normalise();
			Vector3 r = i.Reflect(n);									//Phong model
			//Vector3 h = (l + e) * (1 / (l + e).Norm());				//Blinn-Phong model

			float diffcosine = l.DotProduct(n);
			float speccosine = e.DotProduct(r);							//Phong model
			//float speccosine = h.DotProduct(n);						//Blinn-Phong model

			//Clamp the cosines between 0 and 1.
			if (diffcosine < 0)
			{
				diffcosine = 0;
			}
			if (diffcosine > 1)
			{
				diffcosine = 1;
			}
			if (speccosine < 0)
			{
				speccosine = 0;
			}
			if (speccosine > 1)
			{
				speccosine = 1;
			}

			//Calculate diffuse component.
			outcolour = outcolour + ((*lit_iter)->GetLightColour() * mat->GetDiffuseColour() * diffcosine);

			//Calculate specular component.
			outcolour = outcolour + ((*lit_iter)->GetLightColour() * mat->GetSpecularColour() * pow(speccosine, mat->GetSpecPower()));

			lit_iter++;
		}
	}

	return outcolour;
}

