#include "Common.h"
#include "gles2N64.h"
#include "Types.h"
#include "VI.h"
#include "OpenGL.h"
#include "N64.h"
#include "gSP.h"
#include "gDP.h"
#include "RSP.h"
#include "Debug.h"
#include "Config.h"

VIInfo VI;

void VI_UpdateSize(void)
{
   if (!config.video.force)
   {
      f32 xScale = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_X_SCALE_REG, 0, 12 ), 10 );
      f32 xOffset = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_X_SCALE_REG, 16, 12 ), 10 );

      f32 yScale = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_Y_SCALE_REG, 0, 12 ), 10 );
      f32 yOffset = _FIXED2FLOAT( _SHIFTR( *gfx_info.VI_Y_SCALE_REG, 16, 12 ), 10 );

      u32 hEnd = _SHIFTR( *gfx_info.VI_H_START_REG, 0, 10 );
      u32 hStart = _SHIFTR( *gfx_info.VI_H_START_REG, 16, 10 );

      // These are in half-lines, so shift an extra bit
      u32 vEnd = _SHIFTR( *gfx_info.VI_V_START_REG, 1, 9 );
      u32 vStart = _SHIFTR( *gfx_info.VI_V_START_REG, 17, 9 );

      //Glide does this:
      if (hEnd == hStart) hEnd = (u32)(*gfx_info.VI_WIDTH_REG / xScale);


      VI.width = (u32)(xScale * (hEnd - hStart));
      VI.height = (u32)(yScale * 1.0126582f * (vEnd - vStart));
   }
   else
   {
      VI.width = config.video.width;
      VI.height = config.video.height;
   }

   if (VI.width == 0.0f) VI.width = 320;
   if (VI.height == 0.0f) VI.height = 240;
   VI.rwidth = 1.0f / VI.width;
   VI.rheight = 1.0f / VI.height;


   //add display buffer if doesn't exist
   if (config.ignoreOffscreenRendering)
   {
      unsigned int i;
      //int start = *REG.VI_ORIGIN;

      u32 start = RSP_SegmentToPhysical(*gfx_info.VI_ORIGIN_REG) & 0x00FFFFFF;
      u32 end = min(start + VI.width * VI.height * 4, RDRAMSize);
      for(i = 0; i < VI.displayNum; i++)
      {
         if (VI.display[i].start <= end && VI.display[i].start >= start) break;
         if (start <= VI.display[i].end && start >= VI.display[i].start) break;
      }
      if (i == VI.displayNum)
      {
         //printf("VI IMAGE=%i\n", o);
         VI.display[i%16].start = start;
         VI.display[i%16].end = end;
         VI.displayNum = (VI.displayNum < 16) ? (VI.displayNum+1) : 16;
      }
   }

}

void VI_UpdateScreen(void)
{
   switch(config.updateMode)
   {

      case SCREEN_UPDATE_AT_VI_CHANGE:
         if (*gfx_info.VI_ORIGIN_REG != VI.lastOrigin)
         {
            if (*gfx_info.VI_ORIGIN_REG < VI.lastOrigin || *gfx_info.VI_ORIGIN_REG > VI.lastOrigin+0x2000  )
               OGL_SwapBuffers();

            VI.lastOrigin = *gfx_info.VI_ORIGIN_REG;
         }
         break;

      case SCREEN_UPDATE_AT_VI_UPDATE:
         if (gSP.changed & CHANGED_COLORBUFFER)
         {
            OGL_SwapBuffers();
            gSP.changed &= ~CHANGED_COLORBUFFER;
         }
         break;
   }
}
