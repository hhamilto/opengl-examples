#version 150 // GLSL 150 = OpenGL 3.2

out vec4 fragColor;
in vec2 out_TexCoord; // Vertex texture coordinate
in vec3 out_Color;    // Vertex color
in vec3 out_Normal;   // Normal vector in eye coordinates
in vec4 out_EyeCoord; // Position of fragment in eye coordinates

in float out_Depth;   // Depth of fragment (range 0 through 1)

uniform int HasTex;    // Is there a texture in tex?
uniform int solid;     // first pass should just be solid. 
uniform int fpsLabel;     // are we drawing that fps label?
uniform sampler2D tex; // Diffuse texture
uniform sampler2D depthBufferTex;
uniform mat4 Projection;
uniform float ao_radius;
uniform int ao_samples;
uniform float ao_rangecheck;

#define BIAS .0001

float rand(vec2 co){
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

#define PI 3.1415926535f

void main() 
{

	if(solid == 0){
		vec2 randSeed = out_EyeCoord.xy;
		if(fpsLabel == 0){
			int i = 0;
			float lightness = 1;
			int occluded = 0;

			int radiusRangeSize = ao_samples/2;
			int thetaRangeSize = ao_samples/4;
			int phiRangeSize = ao_samples/4;

			for(i = 0; i < ao_samples; i++){

				float normTheta;
				float normPhi;
				normTheta = atan(out_Normal.y/out_Normal.x);
				normPhi	=	acos(out_Normal.z);

				float theta; //	=	2*PI*u;
				float phi; //	=	acos(2*v-1);
				//use the top points off a icosaedron 
				switch(i%6){
					case 0 :
						theta = 0+normTheta;
						phi = PI+normPhi;
						break;
					case 1 :
						theta = normTheta;
						phi = PI/3+ normPhi;
						break;
					case 2 :
						theta = normTheta+1*2*PI/5;
						phi = PI/3+ normPhi;
						break;
					case 3 :
						theta = normTheta+2*2*PI/5;
						phi = PI/3+ normPhi;
						break;
					case 4 :
						theta = normTheta+3*2*PI/5;
						phi = PI/3+ normPhi;
						break;
					case 5 :
						theta = normTheta+4*2*PI/5;
						phi = PI/3+ normPhi;
						break;
				}

				float radius = ao_radius* 
				(i/6)*(1.0/(ao_samples/6));

				vec3 randSpherePoint = vec3(cos(theta)*sin(phi), sin(theta)*sin(phi), cos(phi));
				
				randSpherePoint = randSpherePoint*radius;
				vec4 samplePoint = vec4(out_EyeCoord.xyz+randSpherePoint, out_EyeCoord.w);

				samplePoint = (Projection * samplePoint);
				samplePoint = samplePoint/samplePoint.w;

				//vec4 samplePoint = out_EyeCoord/out_EyeCoord.w;
				float depth = (texture(depthBufferTex, vec2((samplePoint.x+1)/2,(samplePoint.y+1)/2)).x-.5)*2;
				float fragZ = samplePoint.z;

				if(depth<fragZ-BIAS && fragZ-depth < ao_rangecheck){
					occluded++;
				}
			}
			lightness = lightness * (ao_samples-occluded)/ao_samples;

			// A vector pointing from the position of the fragment to the
			// camera (both in camera coordinates).
			vec3 camLook = normalize(vec3(0,0,0) - out_EyeCoord.xyz);

			// Calculate diffuse lighting:
			float diffuse = dot(camLook, normalize(out_Normal.xyz));
			diffuse = abs(diffuse); // Light front and back the same way:

			// Don't let the diffuse term get too small (otherwise object
			// would be black!). We do this by making the diffuse term range
			// from .5 to 1 instead of 0 to 1
			diffuse = diffuse / 2 + .5;  
			lightness = lightness*diffuse;

			fragColor = vec4(texture(tex, out_TexCoord).rgb*lightness,1);

		}else{
			fragColor = texture(tex, out_TexCoord);
		}

	}else{
		fragColor = vec4(0,0,0,1);
	}
	
}

