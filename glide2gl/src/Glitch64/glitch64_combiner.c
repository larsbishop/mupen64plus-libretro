/*
* Glide64 - Glide video plugin for Nintendo 64 emulators.
* Copyright (c) 2002  Dave2001
* Copyright (c) 2003-2009  Sergey 'Gonetz' Lipski
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <stdint.h>
#ifdef _WIN32
#include <windows.h>
#else // _WIN32
#include <string.h>
#include <stdlib.h>
#endif // _WIN32
#include <math.h>
#include "inc/glide.h"
#include "glitchmain.h"
#include "../../libretro/SDL.h"

float glide64_pow(float a, float b);

typedef struct _shader_program_key
{
   int color_combiner;
   int alpha_combiner;
   int texture0_combiner;
   int texture1_combiner;
   int texture0_combinera;
   int texture1_combinera;
   int fog_enabled;
   int chroma_enabled;
   int dither_enabled;
   int three_point_filter0;
   int three_point_filter1;
   GLuint fragment_shader_object;
   GLuint program_object;
   int texture0_location;
   int texture1_location;
   int vertexOffset_location;
   int textureSizes_location;   
   int exactSizes_location;
   int fogModeEndScale_location;
   int fogColor_location;
   int alphaRef_location;
   int chroma_color_location;
} shader_program_key;

static int fct[4], source0[4], operand0[4], source1[4], operand1[4], source2[4], operand2[4];
static int fcta[4],sourcea0[4],operanda0[4],sourcea1[4],operanda1[4],sourcea2[4],operanda2[4];
static int alpha_ref, alpha_func;
bool alpha_test = 0;

static shader_program_key* shader_programs;
static int number_of_programs = 0;
static int color_combiner_key;
static int alpha_combiner_key;
static int texture0_combiner_key;
static int texture1_combiner_key;
static int texture0_combinera_key;
static int texture1_combinera_key;

float texture_env_color[4];
float ccolor[2][4];
static float chroma_color[4];
int fog_enabled;
static int chroma_enabled;
static int chroma_other_color;
static int chroma_other_alpha;
static int dither_enabled;

float fogStart,fogEnd;

int need_lambda[2];
float lambda_color[2][4];

// shaders variables
int need_to_compile;

static char *fragment_shader;
static GLuint fragment_shader_object;
static GLuint vertex_shader_object;
GLuint program_object_default;
static GLuint program_object;
static int constant_color_location;
static int ccolor0_location;
static int ccolor1_location;
static int first_color = 1;
static int first_alpha = 1;
static int first_texture0 = 1;
static int first_texture1 = 1;
static int tex0_combiner_ext = 0;
static int tex1_combiner_ext = 0;
static int c_combiner_ext = 0;
static int a_combiner_ext = 0;

#if !defined(__LIBRETRO__) || defined(HAVE_OPENGLES2) // Desktop GL fix
#define GLSL_VERSION "100"
#else
#define GLSL_VERSION "120"
#endif

#define SHADER_HEADER \
"#version " GLSL_VERSION          "\n" \
"#define gl_Color vFrontColor      \n" \
"#define gl_FrontColor vFrontColor \n" \
"#define gl_TexCoord vTexCoord     \n" \

#define SHADER_VARYING \
"varying highp vec4 gl_FrontColor;  \n" \
"varying highp vec4 gl_TexCoord[4]; \n"

static const char* fragment_shader_header =
SHADER_HEADER
#if !defined(__LIBRETRO__) || defined(HAVE_OPENGLES2) // Desktop GL fix
"precision lowp float;          \n"
#else
"#define highp                  \n"
#endif
#ifdef EMSCRIPTEN
"#extension GL_EXT_frag_depth : enable\n"
#endif
"uniform sampler2D texture0;    \n"
"uniform sampler2D texture1;    \n"
"uniform vec4 exactSizes;     \n"  //textureSizes doesn't contain the correct sizes, use this one instead for offset calculations
"uniform vec4 constant_color;   \n"
"uniform vec4 ccolor0;          \n"
"uniform vec4 ccolor1;          \n"
"uniform vec4 chroma_color;     \n"
"uniform float lambda;          \n"
"uniform vec3 fogColor;         \n"
"uniform float alphaRef;        \n"
"#define TEX0             texture2D(texture0, gl_TexCoord[0].xy) \n" \
"#define TEX0_OFFSET(off) texture2D(texture0, gl_TexCoord[0].xy - off/exactSizes.xy) \n" \
"#define TEX1             texture2D(texture1, gl_TexCoord[1].xy) \n" \
"#define TEX1_OFFSET(off) texture2D(texture1, gl_TexCoord[1].xy - off/exactSizes.zw) \n" \

SHADER_VARYING
"\n"
"void test_chroma(vec4 ctexture1); \n"
"\n"
"\n"
"void main()\n"
"{\n"
"  vec2 offset; \n"
"  vec4 c0,c1,c2; \n"		
;

// using gl_FragCoord is terribly slow on ATI and varying variables don't work for some unknown
// reason, so we use the unused components of the texture2 coordinates
static const char* fragment_shader_dither =
"  highp float temp=abs(sin((gl_TexCoord[2].a)+sin((gl_TexCoord[2].a)+(gl_TexCoord[2].b))))*170.0; \n"
"  if ((fract(temp)+fract(temp/2.0)+fract(temp/4.0))>1.5) discard; \n"
;

static const char* fragment_shader_default =
"  gl_FragColor = TEX0; \n"
;
static const char* fragment_shader_readtex0color =
"  vec4 readtex0 = TEX0; \n"
;
static const char* fragment_shader_readtex0color_3point =
"  offset=fract(gl_TexCoord[0].xy*exactSizes.xy-vec2(0.5,0.5)); \n"
"  offset-=step(1.0,offset.x+offset.y); \n"
"  c0=TEX0_OFFSET(offset); \n"
"  c1=TEX0_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"  c2=TEX0_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"  vec4 readtex0 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n"
;

static const char* fragment_shader_readtex1color =
"  vec4 readtex1 = TEX1; \n"
;

static const char* fragment_shader_readtex1color_3point =
"  offset=fract(gl_TexCoord[1].xy*exactSizes.zw-vec2(0.5,0.5)); \n"
"  offset-=step(1.0,offset.x+offset.y); \n"
"  c0=TEX1_OFFSET(offset); \n"
"  c1=TEX1_OFFSET(vec2(offset.x-sign(offset.x),offset.y)); \n"
"  c2=TEX1_OFFSET(vec2(offset.x,offset.y-sign(offset.y))); \n"
"  vec4 readtex1 =c0+abs(offset.x)*(c1-c0)+abs(offset.y)*(c2-c0); \n";

static const char* fragment_shader_fog =
"  float fog;  \n"
"  fog = gl_TexCoord[0].b;  \n"
"  gl_FragColor.rgb = mix(fogColor, gl_FragColor.rgb, fog); \n"
;

static const char* fragment_shader_end =
"if(gl_FragColor.a <= alphaRef) {discard;}   \n"
"}\n"
;

static const char* vertex_shader =
SHADER_HEADER
#if defined(__LIBRETRO__) && !defined(HAVE_OPENGLES2) // Desktop GL fix
"#define highp                         \n"
#endif
"#define Z_MAX 65536.0                 \n"
"attribute highp vec4 aPosition;         \n"
"attribute highp vec4 aColor;          \n"
"attribute highp vec4 aMultiTexCoord0; \n"
"attribute highp vec4 aMultiTexCoord1; \n"
"attribute float aFog;                 \n"
"uniform vec3 vertexOffset;            \n" //Moved some calculations from grDrawXXX to shader
"uniform vec4 textureSizes;            \n" 
"uniform vec3 fogModeEndScale;         \n" //0 = Mode, 1 = gl_Fog.end, 2 = gl_Fog.scale
SHADER_VARYING
"\n"
"void main()\n"
"{\n"
"  highp float q = aPosition.w;                                                     \n"
"  highp float invertY = vertexOffset.z;                                          \n" //Usually 1.0 but -1.0 when rendering to a texture (see inverted_culling grRenderBuffer)
"  gl_Position.x = (aPosition.x - vertexOffset.x) / vertexOffset.x;           \n"
"  gl_Position.y = invertY *-(aPosition.y - vertexOffset.y) / vertexOffset.y; \n"
"  gl_Position.z = aPosition.z / Z_MAX;                                       \n"
"  gl_Position.w = 1.0;                                                     \n"
"  gl_Position /= q;                                                        \n"
"  gl_FrontColor = aColor.bgra;                                             \n"
"\n"
"  gl_TexCoord[0] = vec4(aMultiTexCoord0.xy / q / textureSizes.xy,0,1);     \n"
"  gl_TexCoord[1] = vec4(aMultiTexCoord1.xy / q / textureSizes.zw,0,1);     \n"
"\n"
"  float fogV = (1.0 / mix(q,aFog,fogModeEndScale[0])) / 255.0;             \n"
//"  //if(fogMode == 2) {                                                     \n"
//"  //  fogV = 1.0 / aFog / 255                                              \n"
//"  //}                                                                      \n"
"\n"
"  float f = (fogModeEndScale[1] - fogV) * fogModeEndScale[2];              \n"
"  f = clamp(f, 0.0, 1.0);                                                  \n"
"  gl_TexCoord[0].b = f;                                                    \n"
"  gl_TexCoord[2].b = aPosition.x;                                            \n" 
"  gl_TexCoord[2].a = aPosition.y;                                            \n" 
"}                                                                          \n" 
;

static char fragment_shader_color_combiner[1024*2];
static char fragment_shader_alpha_combiner[1024*2];
static char fragment_shader_texture1[1024*2];
static char fragment_shader_texture0[1024*2];
static char fragment_shader_chroma[1024*2];
static char shader_log[2048];

void check_compile(GLuint shader)
{
   GLint success;
   glGetShaderiv(shader,GL_COMPILE_STATUS,&success);

   if (!success)
   {
      char log[1024];
      glGetShaderInfoLog(shader,1024,NULL,log);
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, log);
   }
}

void check_link(GLuint program)
{
   GLint success;
   glGetProgramiv(program,GL_LINK_STATUS,&success);

   if (!success)
   {
      char log[1024];
      glGetProgramInfoLog(program,1024,NULL,log);
      if (log_cb)
         log_cb(RETRO_LOG_ERROR, log);
   }
}

void init_combiner(void)
{
   int texture0_location, texture1_location, log_length;
   char s[128];

   shader_programs = (shader_program_key*)malloc(sizeof(shader_program_key));
   fragment_shader = (char*)malloc(4096*2);

   // default shader
   fragment_shader_object = glCreateShader(GL_FRAGMENT_SHADER);

   strcpy(fragment_shader, fragment_shader_header);
   strcat(fragment_shader, fragment_shader_default);
   strcat(fragment_shader, fragment_shader_end);
   glShaderSource(fragment_shader_object, 1, (const GLchar**)&fragment_shader, NULL);

   glCompileShader(fragment_shader_object);
   check_compile(fragment_shader_object);

   vertex_shader_object = glCreateShader(GL_VERTEX_SHADER);
   glShaderSource(vertex_shader_object, 1, (const GLchar**)&vertex_shader, NULL);
   glCompileShader(vertex_shader_object);
   check_compile(vertex_shader_object);

   program_object = glCreateProgram();
   glAttachShader(program_object, vertex_shader_object);
   glAttachShader(program_object, fragment_shader_object);
   program_object_default = program_object;

   glBindAttribLocation(program_object,POSITION_ATTR,"aPosition");
   glBindAttribLocation(program_object,COLOUR_ATTR,"aColor");
   glBindAttribLocation(program_object,TEXCOORD_0_ATTR,"aMultiTexCoord0");
   glBindAttribLocation(program_object,TEXCOORD_1_ATTR,"aMultiTexCoord1");
   glBindAttribLocation(program_object,FOG_ATTR,"aFog");

   glLinkProgram(program_object);
   check_link(program_object);
   glUseProgram(program_object);

   texture0_location = glGetUniformLocation(program_object, "texture0");
   texture1_location = glGetUniformLocation(program_object, "texture1");
   glUniform1i(texture0_location, 0);
   glUniform1i(texture1_location, 1);

   strcpy(fragment_shader_color_combiner, "");
   strcpy(fragment_shader_alpha_combiner, "");
   strcpy(fragment_shader_texture1, "vec4 ctexture1 = texture2D(texture0, vec2(gl_TexCoord[0])); \n");
   strcpy(fragment_shader_texture0, "");

   first_color = 1;
   first_alpha = 1;
   first_texture0 = 1;
   first_texture1 = 1;
   need_to_compile = 0;
   fog_enabled = 0;
   chroma_enabled = 0;
   dither_enabled = 0;
}

void compile_chroma_shader(void)
{
   strcpy(fragment_shader_chroma, "\nvoid test_chroma(vec4 ctexture1)\n{\n");

   switch(chroma_other_alpha)
   {
      case GR_COMBINE_OTHER_ITERATED:
         strcat(fragment_shader_chroma, "float alpha = gl_Color.a; \n");
         break;
      case GR_COMBINE_OTHER_TEXTURE:
         strcat(fragment_shader_chroma, "float alpha = ctexture1.a; \n");
         break;
      case GR_COMBINE_OTHER_CONSTANT:
         strcat(fragment_shader_chroma, "float alpha = constant_color.a; \n");
         break;
      default:
         DISPLAY_WARNING("unknown compile_choma_shader_alpha : %x", chroma_other_alpha);
   }

   switch(chroma_other_color)
   {
      case GR_COMBINE_OTHER_ITERATED:
         strcat(fragment_shader_chroma, "vec4 color = vec4(vec3(gl_Color),alpha); \n");
         break;
      case GR_COMBINE_OTHER_TEXTURE:
         strcat(fragment_shader_chroma, "vec4 color = vec4(vec3(ctexture1),alpha); \n");
         break;
      case GR_COMBINE_OTHER_CONSTANT:
         strcat(fragment_shader_chroma, "vec4 color = vec4(vec3(constant_color),alpha); \n");
         break;
      default:
         DISPLAY_WARNING("unknown compile_choma_shader_alpha : %x", chroma_other_color);
   }

   strcat(fragment_shader_chroma, "if (color.rgb == chroma_color.rgb) discard; \n");
   strcat(fragment_shader_chroma, "}");
}


void update_uniforms(shader_program_key prog)
{
   GLfloat v0, v1, v2;
   glUniform1i(prog.texture0_location, 0);
   glUniform1i(prog.texture1_location, 1);

   v2 = 1.0f;
   glUniform3f(
      prog.vertexOffset_location,
      (GLfloat)width / 2.f,
      (GLfloat)height / 2.f,
      v2
   );
   glUniform4f(
      prog.textureSizes_location,
      (float)tex_width[0],
      (float)tex_height[0],
      (float)tex_width[1],
      (float)tex_height[1]
   );
   glUniform4f(
      prog.exactSizes_location,
      (float)tex_exactWidth[0],
      (float)tex_exactHeight[0],
      (float)tex_exactWidth[1],
      (float)tex_exactHeight[1]
   );

   v0 = fog_enabled != 2 ? 0.0f : 1.0f;
   v2 /= (fogEnd - fogStart);
   glUniform3f(prog.fogModeEndScale_location, v0, fogEnd,  v2);

   if(prog.fogColor_location != -1)
      glUniform3f(prog.fogColor_location,rdp.fog_color_sep[0] / 255.0f, rdp.fog_color_sep[1] / 255.0f, rdp.fog_color_sep[2] / 255.0f);

   glUniform1f(prog.alphaRef_location,alpha_test ? alpha_ref/255.0f : -1.0f);

   constant_color_location = glGetUniformLocation(program_object, "constant_color");
   glUniform4f(constant_color_location, texture_env_color[0], texture_env_color[1],
         texture_env_color[2], texture_env_color[3]);

   ccolor0_location = glGetUniformLocation(program_object, "ccolor0");
   glUniform4f(ccolor0_location, ccolor[0][0], ccolor[0][1], ccolor[0][2], ccolor[0][3]);

   ccolor1_location = glGetUniformLocation(program_object, "ccolor1");
   glUniform4f(ccolor1_location, ccolor[1][0], ccolor[1][1], ccolor[1][2], ccolor[1][3]);

   glUniform4f(prog.chroma_color_location, chroma_color[0], chroma_color[1],
         chroma_color[2], chroma_color[3]);

   set_lambda();
}

void compile_shader(void)
{
   int vertexOffset_location, textureSizes_location, texture0_location, texture1_location;
   int i, chroma_color_location, log_length;

   need_to_compile = 0;

   for( i = 0; i < number_of_programs; i++)
   {
      shader_program_key prog = shader_programs[i];
      if(prog.color_combiner == color_combiner_key &&
            prog.alpha_combiner == alpha_combiner_key &&
            prog.texture0_combiner == texture0_combiner_key &&
            prog.texture1_combiner == texture1_combiner_key &&
            prog.texture0_combinera == texture0_combinera_key &&
            prog.texture1_combinera == texture1_combinera_key &&
            prog.fog_enabled == fog_enabled &&
            prog.chroma_enabled == chroma_enabled &&
            prog.dither_enabled == dither_enabled &&
			prog.three_point_filter0 == three_point_filter[0] &&
			prog.three_point_filter1 == three_point_filter[1])
      {
         program_object = shader_programs[i].program_object;
         glUseProgram(program_object);
         update_uniforms(prog);
         return;
      }
   }

   shader_programs = (shader_program_key*)realloc(shader_programs, (number_of_programs+1)*sizeof(shader_program_key));

   shader_programs[number_of_programs].color_combiner = color_combiner_key;
   shader_programs[number_of_programs].alpha_combiner = alpha_combiner_key;
   shader_programs[number_of_programs].texture0_combiner = texture0_combiner_key;
   shader_programs[number_of_programs].texture1_combiner = texture1_combiner_key;
   shader_programs[number_of_programs].texture0_combinera = texture0_combinera_key;
   shader_programs[number_of_programs].texture1_combinera = texture1_combinera_key;
   shader_programs[number_of_programs].fog_enabled = fog_enabled;
   shader_programs[number_of_programs].chroma_enabled = chroma_enabled;
   shader_programs[number_of_programs].dither_enabled = dither_enabled;
   shader_programs[number_of_programs].three_point_filter0 = three_point_filter[0];
   shader_programs[number_of_programs].three_point_filter1 = three_point_filter[1];


   strcpy(fragment_shader, fragment_shader_header);
   if(dither_enabled) strcat(fragment_shader, fragment_shader_dither);
   strcat(fragment_shader, three_point_filter[0] ? fragment_shader_readtex0color_3point:fragment_shader_readtex0color);
   strcat(fragment_shader,  three_point_filter[1] ? fragment_shader_readtex1color_3point:fragment_shader_readtex1color);
   strcat(fragment_shader, fragment_shader_texture0);
   strcat(fragment_shader, fragment_shader_texture1);
   strcat(fragment_shader, fragment_shader_color_combiner);
   strcat(fragment_shader, fragment_shader_alpha_combiner);
   if(fog_enabled) strcat(fragment_shader, fragment_shader_fog);
   if(chroma_enabled)
   {
      strcat(fragment_shader, fragment_shader_chroma);
      strcat(fragment_shader_texture1, "test_chroma(ctexture1); \n");
      compile_chroma_shader();
   }
   strcat(fragment_shader, fragment_shader_end);

   shader_programs[number_of_programs].fragment_shader_object = glCreateShader(GL_FRAGMENT_SHADER);
   glShaderSource(shader_programs[number_of_programs].fragment_shader_object, 1, (const GLchar**)&fragment_shader, NULL);

   glCompileShader(shader_programs[number_of_programs].fragment_shader_object);
   check_compile(shader_programs[number_of_programs].fragment_shader_object);

   program_object = glCreateProgram();
   shader_programs[number_of_programs].program_object = program_object;

   glAttachShader(program_object, shader_programs[number_of_programs].fragment_shader_object);
   glAttachShader(program_object, vertex_shader_object);

   glBindAttribLocation(program_object,POSITION_ATTR,"aPosition");
   glBindAttribLocation(program_object,COLOUR_ATTR,"aColor");
   glBindAttribLocation(program_object,TEXCOORD_0_ATTR,"aMultiTexCoord0");
   glBindAttribLocation(program_object,TEXCOORD_1_ATTR,"aMultiTexCoord1");
   glBindAttribLocation(program_object,FOG_ATTR,"aFog");

   glLinkProgram(program_object);
   check_link(program_object);
   glUseProgram(program_object);


   shader_programs[number_of_programs].texture0_location = glGetUniformLocation(program_object, "texture0");
   shader_programs[number_of_programs].texture1_location = glGetUniformLocation(program_object, "texture1");
   shader_programs[number_of_programs].vertexOffset_location = glGetUniformLocation(program_object, "vertexOffset");
   shader_programs[number_of_programs].textureSizes_location = glGetUniformLocation(program_object, "textureSizes");
   shader_programs[number_of_programs].exactSizes_location = glGetUniformLocation(program_object, "exactSizes");
   shader_programs[number_of_programs].fogModeEndScale_location = glGetUniformLocation(program_object, "fogModeEndScale");
   shader_programs[number_of_programs].fogColor_location = glGetUniformLocation(program_object, "fogColor");
   shader_programs[number_of_programs].alphaRef_location = glGetUniformLocation(program_object, "alphaRef");
   shader_programs[number_of_programs].chroma_color_location = glGetUniformLocation(program_object, "chroma_color");

   update_uniforms(shader_programs[number_of_programs]);

   number_of_programs++;
}

void free_combiners(void)
{
   if (shader_programs)
      free(shader_programs);
   if (fragment_shader)
      free(fragment_shader);
   shader_programs = NULL;
   number_of_programs = 0;
}

void set_copy_shader(void)
{
   int texture0_location;
   int alphaRef_location;

   glUseProgram(program_object_default);
   texture0_location = glGetUniformLocation(program_object_default, "texture0");
   glUniform1i(texture0_location, 0);

   alphaRef_location = glGetUniformLocation(program_object_default, "alphaRef");
   if(alphaRef_location != -1)
      glUniform1f(alphaRef_location,alpha_test ? alpha_ref/255.0f : -1.0f);
}

void set_depth_shader(void)
{
}

void set_lambda(void)
{
   int lambda_location = glGetUniformLocation(program_object, "lambda");
   glUniform1f(lambda_location, lambda);
}

FX_ENTRY void FX_CALL 
grConstantColorValue( GrColor_t value )
{
   LOG("grConstantColorValue(%d)\r\n", value);
   texture_env_color[0] = ((value >> 24) & 0xFF) / 255.0f;
   texture_env_color[1] = ((value >> 16) & 0xFF) / 255.0f;
   texture_env_color[2] = ((value >>  8) & 0xFF) / 255.0f;
   texture_env_color[3] = (value & 0xFF) / 255.0f;

   constant_color_location = glGetUniformLocation(program_object, "constant_color");
   glUniform4f(constant_color_location, texture_env_color[0], texture_env_color[1], 
         texture_env_color[2], texture_env_color[3]);
}

void writeGLSLColorOther(int other)
{
   switch(other)
   {
      case GR_COMBINE_OTHER_ITERATED:
         strcat(fragment_shader_color_combiner, "vec4 color_other = gl_Color; \n");
         break;
      case GR_COMBINE_OTHER_TEXTURE:
         strcat(fragment_shader_color_combiner, "vec4 color_other = ctexture1; \n");
         break;
      case GR_COMBINE_OTHER_CONSTANT:
         strcat(fragment_shader_color_combiner, "vec4 color_other = constant_color; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLColorOther : %x", other);
   }
}

void writeGLSLColorLocal(int local)
{
   switch(local)
   {
      case GR_COMBINE_LOCAL_ITERATED:
         strcat(fragment_shader_color_combiner, "vec4 color_local = gl_Color; \n");
         break;
      case GR_COMBINE_LOCAL_CONSTANT:
         strcat(fragment_shader_color_combiner, "vec4 color_local = constant_color; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLColorLocal : %x", local);
   }
}

void writeGLSLColorFactor(int factor, int local, int need_local, int other, int need_other)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(0.0); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         if(need_local) writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = color_local; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         if(need_other) writeGLSLColorOther(other);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(color_other.a); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         if(need_local) writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(color_local.a); \n");
         break;
      case GR_COMBINE_FACTOR_TEXTURE_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(ctexture1.a); \n");
         break;
      case GR_COMBINE_FACTOR_TEXTURE_RGB:
         strcat(fragment_shader_color_combiner, "vec4 color_factor = ctexture1; \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(1.0); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         if(need_local) writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(1.0) - color_local; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         if(need_other) writeGLSLColorOther(other);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(1.0) - vec4(color_other.a); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         if(need_local) writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(1.0) - vec4(color_local.a); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 color_factor = vec4(1.0) - vec4(ctexture1.a); \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLColorFactor : %x", factor);
   }
}

FX_ENTRY void FX_CALL 
grColorCombine(
               GrCombineFunction_t function, GrCombineFactor_t factor,
               GrCombineLocal_t local, GrCombineOther_t other,
               FxBool invert )
{
   static int last_function = 0;
   static int last_factor = 0;
   static int last_local = 0;
   static int last_other = 0;
   LOG("grColorCombine(%d,%d,%d,%d,%d)\r\n", function, factor, local, other, invert);

   if(last_function == function && last_factor == factor &&
         last_local == local && last_other == other && first_color == 0 && !c_combiner_ext)
      return;
   first_color = 0;
   c_combiner_ext = 0;

   last_function = function;
   last_factor = factor;
   last_local = local;
   last_other = other;

   color_combiner_key = function | (factor << 4) | (local << 8) | (other << 10);
   chroma_other_color = other;

   strcpy(fragment_shader_color_combiner, "");
   switch(function)
   {
      case GR_COMBINE_FUNCTION_ZERO:
         strcat(fragment_shader_color_combiner, "gl_FragColor = vec4(0.0); \n");
         break;
      case GR_COMBINE_FUNCTION_LOCAL:
         writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_local; \n");
         break;
      case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
         writeGLSLColorLocal(local);
         strcat(fragment_shader_color_combiner, "gl_FragColor = vec4(color_local.a); \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER:
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,1,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * color_other; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
         writeGLSLColorLocal(local);
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,0,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * color_other + color_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
         writeGLSLColorLocal(local);
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,0,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * color_other + vec4(color_local.a); \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
         writeGLSLColorLocal(local);
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,0,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * (color_other - color_local); \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
         writeGLSLColorLocal(local);
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,0,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * (color_other - color_local) + color_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
         writeGLSLColorLocal(local);
         writeGLSLColorOther(other);
         writeGLSLColorFactor(factor,local,0,other,0);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * (color_other - color_local) + vec4(color_local.a); \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
         writeGLSLColorLocal(local);
         writeGLSLColorFactor(factor,local,0,other,1);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * (-color_local) + color_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
         writeGLSLColorLocal(local);
         writeGLSLColorFactor(factor,local,0,other,1);
         strcat(fragment_shader_color_combiner, "gl_FragColor = color_factor * (-color_local) + vec4(color_local.a); \n");
         break;
      default:
         strcpy(fragment_shader_color_combiner, fragment_shader_default);
         DISPLAY_WARNING("grColorCombine : unknown function : %x", function);
   }

   need_to_compile = 1;
}

void writeGLSLAlphaOther(int other)
{
   switch(other)
   {
      case GR_COMBINE_OTHER_ITERATED:
         strcat(fragment_shader_alpha_combiner, "float alpha_other = gl_Color.a; \n");
         break;
      case GR_COMBINE_OTHER_TEXTURE:
         strcat(fragment_shader_alpha_combiner, "float alpha_other = ctexture1.a; \n");
         break;
      case GR_COMBINE_OTHER_CONSTANT:
         strcat(fragment_shader_alpha_combiner, "float alpha_other = constant_color.a; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLAlphaOther : %x", other);
   }
}

void writeGLSLAlphaLocal(int local)
{
   switch(local)
   {
      case GR_COMBINE_LOCAL_ITERATED:
         strcat(fragment_shader_alpha_combiner, "float alpha_local = gl_Color.a; \n");
         break;
      case GR_COMBINE_LOCAL_CONSTANT:
         strcat(fragment_shader_alpha_combiner, "float alpha_local = constant_color.a; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLAlphaLocal : %x", local);
   }
}

void writeGLSLAlphaFactor(int factor, int local, int need_local, int other, int need_other)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 0.0; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         if(need_local) writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = alpha_local; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         if(need_other) writeGLSLAlphaOther(other);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = alpha_other; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         if(need_local) writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = alpha_local; \n");
         break;
      case GR_COMBINE_FACTOR_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = ctexture1.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 1.0; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         if(need_local) writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 1.0 - alpha_local; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         if(need_other) writeGLSLAlphaOther(other);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 1.0 - alpha_other; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         if(need_local) writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 1.0 - alpha_local; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float alpha_factor = 1.0 - ctexture1.a; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLAlphaFactor : %x", factor);
   }
}

FX_ENTRY void FX_CALL
grAlphaCombine(
               GrCombineFunction_t function, GrCombineFactor_t factor,
               GrCombineLocal_t local, GrCombineOther_t other,
               FxBool invert
               )
{
   static int last_function = 0;
   static int last_factor = 0;
   static int last_local = 0;
   static int last_other = 0;
   LOG("grAlphaCombine(%d,%d,%d,%d,%d)\r\n", function, factor, local, other, invert);

   if(last_function == function && last_factor == factor &&
         last_local == local && last_other == other && first_alpha == 0 && !a_combiner_ext) return;
   first_alpha = 0;
   a_combiner_ext = 0;

   last_function = function;
   last_factor = factor;
   last_local = local;
   last_other = other;

   alpha_combiner_key = function | (factor << 4) | (local << 8) | (other << 10);
   chroma_other_alpha = other;

   strcpy(fragment_shader_alpha_combiner, "");

   switch(function)
   {
      case GR_COMBINE_FUNCTION_ZERO:
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = 0.0; \n");
         break;
      case GR_COMBINE_FUNCTION_LOCAL:
         writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
         writeGLSLAlphaLocal(local);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER:
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,1,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * alpha_other; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,0,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * alpha_other + alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,0,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * alpha_other + alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,0,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * (alpha_other - alpha_local); \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,0,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * (alpha_other - alpha_local) + alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaOther(other);
         writeGLSLAlphaFactor(factor,local,0,other,0);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * (alpha_other - alpha_local) + alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaFactor(factor,local,0,other,1);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * (-alpha_local) + alpha_local; \n");
         break;
      case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
         writeGLSLAlphaLocal(local);
         writeGLSLAlphaFactor(factor,local,0,other,1);
         strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = alpha_factor * (-alpha_local) + alpha_local; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombine : unknown function : %x", function);
   }

   need_to_compile = 1;
}

static void writeGLSLTextureColorFactorTMU0(int num_tex, int factor)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(0.0); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = readtex0; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(0.0); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(readtex0.a); \n");
         break;
      case GR_COMBINE_FACTOR_DETAIL_FACTOR:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(lambda); \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(1.0); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(1.0) - readtex0; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(1.0) - vec4(0.0); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(1.0) - vec4(readtex0.a); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR:
         strcat(fragment_shader_texture0, "vec4 texture0_color_factor = vec4(1.0) - vec4(lambda); \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLTextureColorFactor : %x", factor);
   }
}

static void writeGLSLTextureColorFactorTMU1(int num_tex, int factor)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(0.0); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = readtex1; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(ctexture0.a); \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(readtex1.a); \n");
         break;
      case GR_COMBINE_FACTOR_DETAIL_FACTOR:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(lambda); \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(1.0); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(1.0) - readtex1; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(1.0) - vec4(ctexture0.a); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(1.0) - vec4(readtex1.a); \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR:
         strcat(fragment_shader_texture1, "vec4 texture1_color_factor = vec4(1.0) - vec4(lambda); \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLTextureColorFactor : %x", factor);
   }
}

static void writeGLSLTextureAlphaFactorTMU0(int num_tex, int factor)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 0.0; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = readtex0.a; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 0.0; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = readtex0.a; \n");
         break;
      case GR_COMBINE_FACTOR_DETAIL_FACTOR:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = lambda; \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 1.0; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 1.0 - readtex0.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 1.0 - 0.0; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 1.0 - readtex0.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR:
         strcat(fragment_shader_texture0, "float texture0_alpha_factor = 1.0 - lambda; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLTextureAlphaFactor : %x", factor);
   }
}

static void writeGLSLTextureAlphaFactorTMU1(int num_tex, int factor)
{
   switch(factor)
   {
      case GR_COMBINE_FACTOR_ZERO:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 0.0; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = readtex1.a; \n");
         break;
      case GR_COMBINE_FACTOR_OTHER_ALPHA:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = ctexture0.a; \n");
         break;
      case GR_COMBINE_FACTOR_LOCAL_ALPHA:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = readtex1.a; \n");
         break;
      case GR_COMBINE_FACTOR_DETAIL_FACTOR:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = lambda; \n");
         break;
      case GR_COMBINE_FACTOR_ONE:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 1.0; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 1.0 - readtex1.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 1.0 - ctexture0.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 1.0 - readtex1.a; \n");
         break;
      case GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR:
         strcat(fragment_shader_texture1, "float texture1_alpha_factor = 1.0 - lambda; \n");
         break;
      default:
         DISPLAY_WARNING("unknown writeGLSLTextureAlphaFactor : %x", factor);
   }
}

FX_ENTRY void FX_CALL 
grTexCombine(
             GrChipID_t tmu,
             GrCombineFunction_t rgb_function,
             GrCombineFactor_t rgb_factor, 
             GrCombineFunction_t alpha_function,
             GrCombineFactor_t alpha_factor,
             FxBool rgb_invert,
             FxBool alpha_invert
             )
{
   int num_tex = 0;

   if (tmu == GR_TMU0)
      num_tex = 1;

   ccolor[tmu][0] = ccolor[tmu][1] = ccolor[tmu][2] = ccolor[tmu][3] = 0;

   if(num_tex == 0)
   {
      static int last_function = 0;
      static int last_factor = 0;
      static int last_afunction = 0;
      static int last_afactor = 0;
      static int last_rgb_invert = 0;

      if(last_function == rgb_function && last_factor == rgb_factor &&
            last_afunction == alpha_function && last_afactor == alpha_factor &&
            last_rgb_invert == rgb_invert && first_texture0 == 0 && !tex0_combiner_ext) return;
      first_texture0 = 0;
      tex0_combiner_ext = 0;

      last_function = rgb_function;
      last_factor = rgb_factor;
      last_afunction = alpha_function;
      last_afactor = alpha_factor;
      last_rgb_invert= rgb_invert;
      texture0_combiner_key = rgb_function | (rgb_factor << 4) | 
         (alpha_function << 8) | (alpha_factor << 12) | 
         (rgb_invert << 16);
      texture0_combinera_key = 0;
      strcpy(fragment_shader_texture0, "");

      switch(rgb_function)
      {
         case GR_COMBINE_FUNCTION_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctexture0 = vec4(0.0); \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL:
            strcat(fragment_shader_texture0, "vec4 ctexture0 = readtex0; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctexture0 = vec4(readtex0.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * vec4(0.0); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * vec4(0.0) + readtex0; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * vec4(0.0) + vec4(readtex0.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * (vec4(0.0) - readtex0); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * (vec4(0.0) - readtex0) + readtex0; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * (vec4(0.0) - readtex0) + vec4(readtex0.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * (-readtex0) + readtex0; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU0(num_tex, rgb_factor);
            strcat(fragment_shader_texture0, "vec4 ctexture0 = texture0_color_factor * (-readtex0) + vec4(readtex0.a); \n");
            break;
         default:
            strcat(fragment_shader_texture0, "vec4 ctexture0 = readtex0; \n");
            DISPLAY_WARNING("grTextCombine : unknown rgb function : %x", rgb_function);
      }

      if (rgb_invert)
         strcat(fragment_shader_texture0, "ctexture0 = vec4(1.0) - ctexture0; \n");

      switch(alpha_function)
      {
         case GR_COMBINE_FACTOR_ZERO:
            strcat(fragment_shader_texture0, "ctexture0.a = 0.0; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL:
            strcat(fragment_shader_texture0, "ctexture0.a = readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            strcat(fragment_shader_texture0, "ctexture0.a = readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * 0.0; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * 0.0 + readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * 0.0 + readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * (0.0 - readtex0.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * (0.0 - readtex0.a) + readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * (0.0 - readtex0.a) + readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * (-readtex0.a) + readtex0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU0(num_tex, alpha_factor);
            strcat(fragment_shader_texture0, "ctexture0.a = texture0_alpha_factor * (-readtex0.a) + readtex0.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctexture0.a = readtex0.a; \n");
            DISPLAY_WARNING("grTextCombine : unknown alpha function : %x", alpha_function);
      }

      if (alpha_invert)
         strcat(fragment_shader_texture0, "ctexture0.a = 1.0 - ctexture0.a; \n");

      ccolor0_location = glGetUniformLocation(program_object, "ccolor0");
      glUniform4f(ccolor0_location, 0, 0, 0, 0);
   }
   else
   {
      static int last_function = 0;
      static int last_factor = 0;
      static int last_afunction = 0;
      static int last_afactor = 0;
      static int last_rgb_invert = 0;

      if(last_function == rgb_function && last_factor == rgb_factor &&
            last_afunction == alpha_function && last_afactor == alpha_factor &&
            last_rgb_invert == rgb_invert && first_texture1 == 0 && !tex1_combiner_ext) return;
      first_texture1 = 0;
      tex1_combiner_ext = 0;

      last_function = rgb_function;
      last_factor = rgb_factor;
      last_afunction = alpha_function;
      last_afactor = alpha_factor;
      last_rgb_invert = rgb_invert;

      texture1_combiner_key = rgb_function | (rgb_factor << 4) | 
         (alpha_function << 8) | (alpha_factor << 12) |
         (rgb_invert << 16);
      texture1_combinera_key = 0;
      strcpy(fragment_shader_texture1, "");

      switch(rgb_function)
      {
         case GR_COMBINE_FUNCTION_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctexture1 = vec4(0.0); \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL:
            strcat(fragment_shader_texture1, "vec4 ctexture1 = readtex1; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctexture1 = vec4(readtex1.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * ctexture0; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * ctexture0 + readtex1; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * ctexture0 + vec4(readtex1.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * (ctexture0 - readtex1); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * (ctexture0 - readtex1) + readtex1; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * (ctexture0 - readtex1) + vec4(readtex1.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * (-readtex1) + readtex1; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureColorFactorTMU1(num_tex, rgb_factor);
            strcat(fragment_shader_texture1, "vec4 ctexture1 = texture1_color_factor * (-readtex1) + vec4(readtex1.a); \n");
            break;
         default:
            strcat(fragment_shader_texture1, "vec4 ctexture1 = readtex1; \n");
            DISPLAY_WARNING("grTextCombine : unknown rgb function : %x", rgb_function);
      }

      if (rgb_invert)
         strcat(fragment_shader_texture1, "ctexture1 = vec4(1.0) - ctexture1; \n");
      
      switch(alpha_function)
      {
         case GR_COMBINE_FACTOR_ZERO:
            strcat(fragment_shader_texture1, "ctexture1.a = 0.0; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL:
            strcat(fragment_shader_texture1, "ctexture1.a = readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
            strcat(fragment_shader_texture1, "ctexture1.a = readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * ctexture0.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * ctexture0.a + readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * ctexture0.a + readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * (ctexture0.a - readtex1.a); \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * (ctexture0.a - readtex1.a) + readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * (ctexture0.a - readtex1.a) + readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * (-readtex1.a) + readtex1.a; \n");
            break;
         case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
            writeGLSLTextureAlphaFactorTMU1(num_tex, alpha_factor);
            strcat(fragment_shader_texture1, "ctexture1.a = texture1_alpha_factor * (-readtex1.a) + readtex1.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctexture1.a = ctexture0.a; \n");
            DISPLAY_WARNING("grTextCombine : unknown alpha function : %x", alpha_function);
      }

      if (alpha_invert)
         strcat(fragment_shader_texture1, "ctexture1.a = 1.0 - ctexture1.a; \n");

      ccolor1_location = glGetUniformLocation(program_object, "ccolor1");
      glUniform4f(ccolor1_location, 0, 0, 0, 0);
   }

   need_to_compile = 1;
}

FX_ENTRY void FX_CALL
grAlphaTestReferenceValue(GrAlpha_t value)
{
   alpha_ref = value;
}

FX_ENTRY void FX_CALL
grAlphaTestFunction( GrCmpFnc_t function, GrAlpha_t value, int set_alpha_ref)
{
   alpha_func = function;
   alpha_test = (function == GR_CMP_ALWAYS) ? false : true;
   alpha_ref = (set_alpha_ref) ? value : alpha_ref;
}

FX_ENTRY void FX_CALL 
grFogMode( GrFogMode_t mode, GrColor_t fogcolor)
{
   fog_enabled = mode;

   need_to_compile = 1;
}

// chroma

FX_ENTRY void FX_CALL 
grChromakeyMode( GrChromakeyMode_t mode )
{
   LOG("grChromakeyMode(%d)\r\n", mode);
   switch(mode)
   {
      case GR_CHROMAKEY_DISABLE:
         chroma_enabled = 0;
         break;
      case GR_CHROMAKEY_ENABLE:
         chroma_enabled = 1;
         break;
      default:
         DISPLAY_WARNING("grChromakeyMode : unknown mode : %x", mode);
   }
   need_to_compile = 1;
}

FX_ENTRY void FX_CALL 
grChromakeyValue( GrColor_t value )
{
   int chroma_color_location = glGetUniformLocation(program_object, "chroma_color");

   chroma_color[0] = ((value >> 24) & 0xFF) / 255.0f;
   chroma_color[1] = ((value >> 16) & 0xFF) / 255.0f;
   chroma_color[2] = ((value >>  8) & 0xFF) / 255.0f;
   chroma_color[3] = 1.0;//(value & 0xFF) / 255.0f;

   glUniform4f(chroma_color_location, chroma_color[0], chroma_color[1],
         chroma_color[2], chroma_color[3]);
}

FX_ENTRY void FX_CALL
grStipplePattern(
                 GrStipplePattern_t stipple)
{
   LOG("grStipplePattern(%x)\r\n", stipple);
}

FX_ENTRY void FX_CALL
grStippleMode( GrStippleMode_t mode )
{
   LOG("grStippleMode(%d)\r\n", mode);
   switch(mode)
   {
      case GR_STIPPLE_DISABLE:
         dither_enabled = 0;
         break;
      case GR_STIPPLE_PATTERN:
      case GR_STIPPLE_ROTATE:
         dither_enabled = 1;
         break;
      default:
         DISPLAY_WARNING("grStippleMode:%x", mode);
   }
   need_to_compile = 1;
}

FX_ENTRY void FX_CALL 
grColorCombineExt(GrCCUColor_t a, GrCombineMode_t a_mode,
                  GrCCUColor_t b, GrCombineMode_t b_mode,
                  GrCCUColor_t c, FxBool c_invert,
                  GrCCUColor_t d, FxBool d_invert,
                  FxU32 shift, FxBool invert)
{
   color_combiner_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
      ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
      ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
      ((d & 0x1F) << 20) | ((d_invert & 1) << 25);
   c_combiner_ext = 1;
   strcpy(fragment_shader_color_combiner, "");

   switch(a)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = vec4(0.0); \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = vec4(ctexture1.a); \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = vec4(constant_color.a); \n");
         break;
      case GR_CMBX_CONSTANT_COLOR:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = constant_color; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = vec4(gl_Color.a); \n");
         break;
      case GR_CMBX_ITRGB:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = gl_Color; \n");
         break;
      case GR_CMBX_TEXTURE_RGB:
         strcat(fragment_shader_color_combiner, "vec4 cs_a = ctexture1; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : a = %x", a);
         strcat(fragment_shader_color_combiner, "vec4 cs_a = vec4(0.0); \n");
   }

   switch(a_mode)
   {
      case GR_FUNC_MODE_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 c_a = vec4(0.0); \n");
         break;
      case GR_FUNC_MODE_X:
         strcat(fragment_shader_color_combiner, "vec4 c_a = cs_a; \n");
         break;
      case GR_FUNC_MODE_ONE_MINUS_X:
         strcat(fragment_shader_color_combiner, "vec4 c_a = vec4(1.0) - cs_a; \n");
         break;
      case GR_FUNC_MODE_NEGATIVE_X:
         strcat(fragment_shader_color_combiner, "vec4 c_a = -cs_a; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : a_mode = %x", a_mode);
         strcat(fragment_shader_color_combiner, "vec4 c_a = vec4(0.0); \n");
   }

   switch(b)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = vec4(0.0); \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = vec4(ctexture1.a); \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = vec4(constant_color.a); \n");
         break;
      case GR_CMBX_CONSTANT_COLOR:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = constant_color; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = vec4(gl_Color.a); \n");
         break;
      case GR_CMBX_ITRGB:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = gl_Color; \n");
         break;
      case GR_CMBX_TEXTURE_RGB:
         strcat(fragment_shader_color_combiner, "vec4 cs_b = ctexture1; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : b = %x", b);
         strcat(fragment_shader_color_combiner, "vec4 cs_b = vec4(0.0); \n");
   }

   switch(b_mode)
   {
      case GR_FUNC_MODE_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 c_b = vec4(0.0); \n");
         break;
      case GR_FUNC_MODE_X:
         strcat(fragment_shader_color_combiner, "vec4 c_b = cs_b; \n");
         break;
      case GR_FUNC_MODE_ONE_MINUS_X:
         strcat(fragment_shader_color_combiner, "vec4 c_b = vec4(1.0) - cs_b; \n");
         break;
      case GR_FUNC_MODE_NEGATIVE_X:
         strcat(fragment_shader_color_combiner, "vec4 c_b = -cs_b; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : b_mode = %x", b_mode);
         strcat(fragment_shader_color_combiner, "vec4 c_b = vec4(0.0); \n");
   }

   switch(c)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(0.0); \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(ctexture1.a); \n");
         break;
      case GR_CMBX_ALOCAL:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(c_b.a); \n");
         break;
      case GR_CMBX_AOTHER:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(c_a.a); \n");
         break;
      case GR_CMBX_B:
         strcat(fragment_shader_color_combiner, "vec4 c_c = cs_b; \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(constant_color.a); \n");
         break;
      case GR_CMBX_CONSTANT_COLOR:
         strcat(fragment_shader_color_combiner, "vec4 c_c = constant_color; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(gl_Color.a); \n");
         break;
      case GR_CMBX_ITRGB:
         strcat(fragment_shader_color_combiner, "vec4 c_c = gl_Color; \n");
         break;
      case GR_CMBX_TEXTURE_RGB:
         strcat(fragment_shader_color_combiner, "vec4 c_c = ctexture1; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : c = %x", c);
         strcat(fragment_shader_color_combiner, "vec4 c_c = vec4(0.0); \n");
   }

   if(c_invert)
      strcat(fragment_shader_color_combiner, "c_c = vec4(1.0) - c_c; \n");

   switch(d)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_color_combiner, "vec4 c_d = vec4(0.0); \n");
         break;
      case GR_CMBX_ALOCAL:
         strcat(fragment_shader_color_combiner, "vec4 c_d = vec4(c_b.a); \n");
         break;
      case GR_CMBX_B:
         strcat(fragment_shader_color_combiner, "vec4 c_d = cs_b; \n");
         break;
      case GR_CMBX_TEXTURE_RGB:
         strcat(fragment_shader_color_combiner, "vec4 c_d = ctexture1; \n");
         break;
      case GR_CMBX_ITRGB:
         strcat(fragment_shader_color_combiner, "vec4 c_d = gl_Color; \n");
         break;
      default:
         DISPLAY_WARNING("grColorCombineExt : d = %x", d);
         strcat(fragment_shader_color_combiner, "vec4 c_d = vec4(0.0); \n");
   }

   if(d_invert)
      strcat(fragment_shader_color_combiner, "c_d = vec4(1.0) - c_d; \n");

   strcat(fragment_shader_color_combiner, "gl_FragColor = (c_a + c_b) * c_c + c_d; \n");

   need_to_compile = 1;
}

FX_ENTRY void FX_CALL
grAlphaCombineExt(GrACUColor_t a, GrCombineMode_t a_mode,
                  GrACUColor_t b, GrCombineMode_t b_mode,
                  GrACUColor_t c, FxBool c_invert,
                  GrACUColor_t d, FxBool d_invert,
                  FxU32 shift, FxBool invert)
{
   alpha_combiner_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
      ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
      ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
      ((d & 0x1F) << 20) | ((d_invert & 1) << 25);
   a_combiner_ext = 1;
   strcpy(fragment_shader_alpha_combiner, "");

   switch(a)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_alpha_combiner, "float as_a = 0.0; \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_a = ctexture1.a; \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_a = constant_color.a; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_a = gl_Color.a; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : a = %x", a);
         strcat(fragment_shader_alpha_combiner, "float as_a = 0.0; \n");
   }

   switch(a_mode)
   {
      case GR_FUNC_MODE_ZERO:
         strcat(fragment_shader_alpha_combiner, "float a_a = 0.0; \n");
         break;
      case GR_FUNC_MODE_X:
         strcat(fragment_shader_alpha_combiner, "float a_a = as_a; \n");
         break;
      case GR_FUNC_MODE_ONE_MINUS_X:
         strcat(fragment_shader_alpha_combiner, "float a_a = 1.0 - as_a; \n");
         break;
      case GR_FUNC_MODE_NEGATIVE_X:
         strcat(fragment_shader_alpha_combiner, "float a_a = -as_a; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : a_mode = %x", a_mode);
         strcat(fragment_shader_alpha_combiner, "float a_a = 0.0; \n");
   }

   switch(b)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_alpha_combiner, "float as_b = 0.0; \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_b = ctexture1.a; \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_b = constant_color.a; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_alpha_combiner, "float as_b = gl_Color.a; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : b = %x", b);
         strcat(fragment_shader_alpha_combiner, "float as_b = 0.0; \n");
   }

   switch(b_mode)
   {
      case GR_FUNC_MODE_ZERO:
         strcat(fragment_shader_alpha_combiner, "float a_b = 0.0; \n");
         break;
      case GR_FUNC_MODE_X:
         strcat(fragment_shader_alpha_combiner, "float a_b = as_b; \n");
         break;
      case GR_FUNC_MODE_ONE_MINUS_X:
         strcat(fragment_shader_alpha_combiner, "float a_b = 1.0 - as_b; \n");
         break;
      case GR_FUNC_MODE_NEGATIVE_X:
         strcat(fragment_shader_alpha_combiner, "float a_b = -as_b; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : b_mode = %x", b_mode);
         strcat(fragment_shader_alpha_combiner, "float a_b = 0.0; \n");
   }

   switch(c)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_alpha_combiner, "float a_c = 0.0; \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float a_c = ctexture1.a; \n");
         break;
      case GR_CMBX_ALOCAL:
         strcat(fragment_shader_alpha_combiner, "float a_c = as_b; \n");
         break;
      case GR_CMBX_AOTHER:
         strcat(fragment_shader_alpha_combiner, "float a_c = as_a; \n");
         break;
      case GR_CMBX_B:
         strcat(fragment_shader_alpha_combiner, "float a_c = as_b; \n");
         break;
      case GR_CMBX_CONSTANT_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float a_c = constant_color.a; \n");
         break;
      case GR_CMBX_ITALPHA:
         strcat(fragment_shader_alpha_combiner, "float a_c = gl_Color.a; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : c = %x", c);
         strcat(fragment_shader_alpha_combiner, "float a_c = 0.0; \n");
   }

   if(c_invert)
      strcat(fragment_shader_alpha_combiner, "a_c = 1.0 - a_c; \n");

   switch(d)
   {
      case GR_CMBX_ZERO:
         strcat(fragment_shader_alpha_combiner, "float a_d = 0.0; \n");
         break;
      case GR_CMBX_TEXTURE_ALPHA:
         strcat(fragment_shader_alpha_combiner, "float a_d = ctexture1.a; \n");
         break;
      case GR_CMBX_ALOCAL:
         strcat(fragment_shader_alpha_combiner, "float a_d = as_b; \n");
         break;
      case GR_CMBX_B:
         strcat(fragment_shader_alpha_combiner, "float a_d = as_b; \n");
         break;
      default:
         DISPLAY_WARNING("grAlphaCombineExt : d = %x", d);
         strcat(fragment_shader_alpha_combiner, "float a_d = 0.0; \n");
   }

   if(d_invert)
      strcat(fragment_shader_alpha_combiner, "a_d = 1.0 - a_d; \n");

   strcat(fragment_shader_alpha_combiner, "gl_FragColor.a = (a_a + a_b) * a_c + a_d; \n");

   need_to_compile = 1;
}

FX_ENTRY void FX_CALL 
grTexColorCombineExt(GrChipID_t       tmu,
                     GrTCCUColor_t a, GrCombineMode_t a_mode,
                     GrTCCUColor_t b, GrCombineMode_t b_mode,
                     GrTCCUColor_t c, FxBool c_invert,
                     GrTCCUColor_t d, FxBool d_invert,
                     FxU32 shift, FxBool invert)
{
   int num_tex = 0;

   if (tmu == GR_TMU0)
      num_tex = 1;

   if(num_tex == 0)
   {
      texture0_combiner_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
         ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
         ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
         ((d & 0x1F) << 20) | ((d_invert & 1) << 25);
      tex0_combiner_ext = 1;
      strcpy(fragment_shader_texture0, "");

      switch(a)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(0.0); \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(readtex0.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = readtex0; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(0.0); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(0.0); \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = ccolor0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(ccolor0.a); \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : a = %x", a);
            strcat(fragment_shader_texture0, "vec4 ctex0s_a = vec4(0.0); \n");
      }

      switch(a_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0_a = vec4(0.0); \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_a = ctex0s_a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_a = vec4(1.0) - ctex0s_a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_a = -ctex0s_a; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : a_mode = %x", a_mode);
            strcat(fragment_shader_texture0, "vec4 ctex0_a = vec4(0.0); \n");
      }

      switch(b)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(0.0); \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(readtex0.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = readtex0; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(0.0); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(0.0); \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(ccolor0.a); \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = ccolor0; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : b = %x", b);
            strcat(fragment_shader_texture0, "vec4 ctex0s_b = vec4(0.0); \n");
      }

      switch(b_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0_b = vec4(0.0); \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_b = ctex0s_b; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_b = vec4(1.0) - ctex0s_b; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture0, "vec4 ctex0_b = -ctex0s_b; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : b_mode = %x", b_mode);
            strcat(fragment_shader_texture0, "vec4 ctex0_b = vec4(0.0); \n");
      }

      switch(c)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(0.0); \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = ctex0s_b; \n");
            break;
         case GR_CMBX_DETAIL_FACTOR:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(lambda); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = gl_Color; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(readtex0.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = readtex0; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(0.0); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(0.0); \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(ccolor0.a); \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture0, "vec4 ctex0_c = ccolor0; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : c = %x", c);
            strcat(fragment_shader_texture0, "vec4 ctex0_c = vec4(0.0); \n");
      }

      if(c_invert)
         strcat(fragment_shader_texture0, "ctex0_c = vec4(1.0) - ctex0_c; \n");

      switch(d)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "vec4 ctex0_d = vec4(0.0); \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture0, "vec4 ctex0_d = ctex0s_b; \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture0, "vec4 ctex0_d = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "vec4 ctex0_d = vec4(readtex0.a); \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : d = %x", d);
            strcat(fragment_shader_texture0, "vec4 ctex0_d = vec4(0.0); \n");
      }

      if(d_invert)
         strcat(fragment_shader_texture0, "ctex0_d = vec4(1.0) - ctex0_d; \n");

      strcat(fragment_shader_texture0, "vec4 ctexture0 = (ctex0_a + ctex0_b) * ctex0_c + ctex0_d; \n");
   }
   else
   {
      texture1_combiner_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
         ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
         ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
         ((d & 0x1F) << 20) | ((d_invert & 1) << 25);
      tex1_combiner_ext = 1;
      strcpy(fragment_shader_texture1, "");

      switch(a)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(0.0); \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(readtex1.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = readtex1; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(ctexture0.a); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = ctexture0; \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = ccolor1; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(ccolor1.a); \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : a = %x", a);
            strcat(fragment_shader_texture1, "vec4 ctex1s_a = vec4(0.0); \n");
      }

      switch(a_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1_a = vec4(0.0); \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_a = ctex1s_a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_a = vec4(1.0) - ctex1s_a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_a = -ctex1s_a; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : a_mode = %x", a_mode);
            strcat(fragment_shader_texture1, "vec4 ctex1_a = vec4(0.0); \n");
      }

      switch(b)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(0.0); \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(readtex1.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = readtex1; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(ctexture0.a); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = ctexture0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(ccolor1.a); \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = ccolor1; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : b = %x", b);
            strcat(fragment_shader_texture1, "vec4 ctex1s_b = vec4(0.0); \n");
      }

      switch(b_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1_b = vec4(0.0); \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_b = ctex1s_b; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_b = vec4(1.0) - ctex1s_b; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture1, "vec4 ctex1_b = -ctex1s_b; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : b_mode = %x", b_mode);
            strcat(fragment_shader_texture1, "vec4 ctex1_b = vec4(0.0); \n");
      }

      switch(c)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(0.0); \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = ctex1s_b; \n");
            break;
         case GR_CMBX_DETAIL_FACTOR:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(lambda); \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = gl_Color; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(gl_Color.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(readtex1.a); \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = readtex1; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(ctexture0.a); \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_RGB:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = ctexture0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(ccolor1.a); \n");
            break;
         case GR_CMBX_TMU_CCOLOR:
            strcat(fragment_shader_texture1, "vec4 ctex1_c = ccolor1; \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : c = %x", c);
            strcat(fragment_shader_texture1, "vec4 ctex1_c = vec4(0.0); \n");
      }

      if(c_invert)
         strcat(fragment_shader_texture1, "ctex1_c = vec4(1.0) - ctex1_c; \n");

      switch(d)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "vec4 ctex1_d = vec4(0.0); \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture1, "vec4 ctex1_d = ctex1s_b; \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture1, "vec4 ctex1_d = gl_Color; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "vec4 ctex1_d = vec4(readtex1.a); \n");
            break;
         default:
            DISPLAY_WARNING("grTexColorCombineExt : d = %x", d);
            strcat(fragment_shader_texture1, "vec4 ctex1_d = vec4(0.0); \n");
      }

      if(d_invert)
         strcat(fragment_shader_texture1, "ctex1_d = vec4(1.0) - ctex1_d; \n");

      strcat(fragment_shader_texture1, "vec4 ctexture1 = (ctex1_a + ctex1_b) * ctex1_c + ctex1_d; \n");
   }

   need_to_compile = 1;
}

FX_ENTRY void FX_CALL 
grTexAlphaCombineExt(GrChipID_t       tmu,
                     GrTACUColor_t a, GrCombineMode_t a_mode,
                     GrTACUColor_t b, GrCombineMode_t b_mode,
                     GrTACUColor_t c, FxBool c_invert,
                     GrTACUColor_t d, FxBool d_invert,
                     FxU32 shift, FxBool invert,
                        GrColor_t     ccolor_value)
{
   int num_tex = 0;

   if (tmu == GR_TMU0)
      num_tex = 1;

   ccolor[num_tex][0] = ((ccolor_value >> 24) & 0xFF) / 255.0f;
   ccolor[num_tex][1] = ((ccolor_value >> 16) & 0xFF) / 255.0f;
   ccolor[num_tex][2] = ((ccolor_value >>  8) & 0xFF) / 255.0f;
   ccolor[num_tex][3] = (ccolor_value & 0xFF) / 255.0f;

   if(num_tex == 0)
   {
      texture0_combinera_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
         ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
         ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
         ((d & 0x1F) << 20) | ((d_invert & 1) << 25);

      switch(a)
      {
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "ctex0s_a.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0s_a.a = readtex0.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0s_a.a = 0.0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "ctex0s_a.a = ccolor0.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0s_a.a = 0.0; \n");
      }

      switch(a_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture0, "ctex0_a.a = 0.0; \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture0, "ctex0_a.a = ctex0s_a.a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture0, "ctex0_a.a = 1.0 - ctex0s_a.a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture0, "ctex0_a.a = -ctex0s_a.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0_a.a = 0.0; \n");
      }

      switch(b)
      {
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "ctex0s_b.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0s_b.a = readtex0.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0s_b.a = 0.0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "ctex0s_b.a = ccolor0.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0s_b.a = 0.0; \n");
      }

      switch(b_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture0, "ctex0_b.a = 0.0; \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture0, "ctex0_b.a = ctex0s_b.a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture0, "ctex0_b.a = 1.0 - ctex0s_b.a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture0, "ctex0_b.a = -ctex0s_b.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0_b.a = 0.0; \n");
      }

      switch(c)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "ctex0_c.a = 0.0; \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture0, "ctex0_c.a = ctex0s_b.a; \n");
            break;
         case GR_CMBX_DETAIL_FACTOR:
            strcat(fragment_shader_texture0, "ctex0_c.a = lambda; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "ctex0_c.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0_c.a = readtex0.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0_c.a = 0.0; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture0, "ctex0_c.a = ccolor0.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0_c.a = 0.0; \n");
      }

      switch(d)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture0, "ctex0_d.a = 0.0; \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture0, "ctex0_d.a = ctex0s_b.a; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture0, "ctex0_d.a = gl_Color.a; \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture0, "ctex0_d.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture0, "ctex0_d.a = readtex0.a; \n");
            break;
         default:
            strcat(fragment_shader_texture0, "ctex0_d.a = 0.0; \n");
      }

      if(c_invert)
         strcat(fragment_shader_texture0, "ctex0_c.a = 1.0 - ctex0_c.a; \n");

      if(d_invert)
         strcat(fragment_shader_texture0, "ctex0_d.a = 1.0 - ctex0_d.a; \n");

      strcat(fragment_shader_texture0, "ctexture0.a = (ctex0_a.a + ctex0_b.a) * ctex0_c.a + ctex0_d.a; \n");

      ccolor0_location = glGetUniformLocation(program_object, "ccolor0");
      glUniform4f(ccolor0_location, ccolor[0][0], ccolor[0][1], ccolor[0][2], ccolor[0][3]);
   }
   else
   {
      texture1_combinera_key = 0x80000000 | (a & 0x1F) | ((a_mode & 3) << 5) | 
         ((b & 0x1F) << 7) | ((b_mode & 3) << 12) |
         ((c & 0x1F) << 14) | ((c_invert & 1) << 19) |
         ((d & 0x1F) << 20) | ((d_invert & 1) << 25);

      switch(a)
      {
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "ctex1s_a.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1s_a.a = readtex1.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1s_a.a = ctexture0.a; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "ctex1s_a.a = ccolor1.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1s_a.a = 0.0; \n");
      }

      switch(a_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture1, "ctex1_a.a = 0.0; \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture1, "ctex1_a.a = ctex1s_a.a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture1, "ctex1_a.a = 1.0 - ctex1s_a.a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture1, "ctex1_a.a = -ctex1s_a.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1_a.a = 0.0; \n");
      }

      switch(b)
      {
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "ctex1s_b.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1s_b.a = readtex1.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1s_b.a = ctexture0.a; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "ctex1s_b.a = ccolor1.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1s_b.a = 0.0; \n");
      }

      switch(b_mode)
      {
         case GR_FUNC_MODE_ZERO:
            strcat(fragment_shader_texture1, "ctex1_b.a = 0.0; \n");
            break;
         case GR_FUNC_MODE_X:
            strcat(fragment_shader_texture1, "ctex1_b.a = ctex1s_b.a; \n");
            break;
         case GR_FUNC_MODE_ONE_MINUS_X:
            strcat(fragment_shader_texture1, "ctex1_b.a = 1.0 - ctex1s_b.a; \n");
            break;
         case GR_FUNC_MODE_NEGATIVE_X:
            strcat(fragment_shader_texture1, "ctex1_b.a = -ctex1s_b.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1_b.a = 0.0; \n");
      }

      switch(c)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "ctex1_c.a = 0.0; \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture1, "ctex1_c.a = ctex1s_b.a; \n");
            break;
         case GR_CMBX_DETAIL_FACTOR:
            strcat(fragment_shader_texture1, "ctex1_c.a = lambda; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "ctex1_c.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1_c.a = readtex1.a; \n");
            break;
         case GR_CMBX_OTHER_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1_c.a = ctexture0.a; \n");
            break;
         case GR_CMBX_TMU_CALPHA:
            strcat(fragment_shader_texture1, "ctex1_c.a = ccolor1.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1_c.a = 0.0; \n");
      }

      switch(d)
      {
         case GR_CMBX_ZERO:
            strcat(fragment_shader_texture1, "ctex1_d.a = 0.0; \n");
            break;
         case GR_CMBX_B:
            strcat(fragment_shader_texture1, "ctex1_d.a = ctex1s_b.a; \n");
            break;
         case GR_CMBX_ITALPHA:
            strcat(fragment_shader_texture1, "ctex1_d.a = gl_Color.a; \n");
            break;
         case GR_CMBX_ITRGB:
            strcat(fragment_shader_texture1, "ctex1_d.a = gl_Color.a; \n");
            break;
         case GR_CMBX_LOCAL_TEXTURE_ALPHA:
            strcat(fragment_shader_texture1, "ctex1_d.a = readtex1.a; \n");
            break;
         default:
            strcat(fragment_shader_texture1, "ctex1_d.a = 0.0; \n");
      }

      if(c_invert)
         strcat(fragment_shader_texture1, "ctex1_c.a = 1.0 - ctex1_c.a; \n");

      if(d_invert)
         strcat(fragment_shader_texture1, "ctex1_d.a = 1.0 - ctex1_d.a; \n");

      strcat(fragment_shader_texture1, "ctexture1.a = (ctex1_a.a + ctex1_b.a) * ctex1_c.a + ctex1_d.a; \n");

      ccolor1_location = glGetUniformLocation(program_object, "ccolor1");
      glUniform4f(ccolor1_location, ccolor[1][0], ccolor[1][1], ccolor[1][2], ccolor[1][3]);
   }

   need_to_compile = 1;
}
