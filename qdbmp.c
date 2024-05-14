/*
**
** Adapted from the qdbmp code; see QDBMP license information below
**
** This is a starting point for full BMP support. Currently, this reads in the palette and handles
** 32, 24, and 8bpp (non-indexed) to RGBA format. Later implementations should handle other, less popular, BMP versions.
**
**
** This file is part of Bevara Access Filters.
**
** This file is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License as published by the Free Software Foundation.
**
** This file is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License along with this file. If not, see <https://www.gnu.org/licenses/>.
*/

#include <gpac/filters.h>
#include "qdbmp.h"

#include <stdio.h>

typedef struct
{
	GF_FilterPid *ipid, *opid;
	Bool is_playing;
	Bool initial_play_done;
} GF_QDBMPCtx;

/* Size of the palette data for 8 BPP bitmaps */
#define BMP_PALETTE_SIZE_8bpp ( 256 * 4 )

/* Size of the palette data for 4 BPP bitmaps */
#define BMP_PALETTE_SIZE_4bpp ( 16 * 4 )

/* Holds the last error code */
static BMP_STATUS BMP_LAST_ERROR_CODE = BMP_OK;




/**************************************************************
	Reads a little-endian unsigned int from the file.
	Returns non-zero on success.
**************************************************************/
int	ReadUINT( UINT* x, FILE* f )
{
	UCHAR little[ 4 ];	/* BMPs use 32 bit ints */

	if ( x == NULL || f == NULL )
	{
		return 0;
	}

	if ( fread( little, 4, 1, f ) != 1 )
	{
		return 0;
	}

	*x = ( little[ 3 ] << 24 | little[ 2 ] << 16 | little[ 1 ] << 8 | little[ 0 ] );

	return 1;
}


/**************************************************************
	Reads a little-endian unsigned short int from the file.
	Returns non-zero on success.
**************************************************************/
int	ReadUSHORT( USHORT *x, FILE* f )
{
	UCHAR little[ 2 ];	/* BMPs use 16 bit shorts */

	if ( x == NULL || f == NULL )
	{
		return 0;
	}

	if ( fread( little, 2, 1, f ) != 1 )
	{
		return 0;
	}

	*x = ( little[ 1 ] << 8 | little[ 0 ] );

	return 1;
}


/**************************************************************
	Writes a little-endian unsigned int to the file.
	Returns non-zero on success.
**************************************************************/
int	WriteUINT( UINT x, FILE* f )
{
	UCHAR little[ 4 ];	/* BMPs use 32 bit ints */

	little[ 3 ] = (UCHAR)( ( x & 0xff000000 ) >> 24 );
	little[ 2 ] = (UCHAR)( ( x & 0x00ff0000 ) >> 16 );
	little[ 1 ] = (UCHAR)( ( x & 0x0000ff00 ) >> 8 );
	little[ 0 ] = (UCHAR)( ( x & 0x000000ff ) >> 0 );

	return ( f && fwrite( little, 4, 1, f ) == 1 );
}


/**************************************************************
	Writes a little-endian unsigned short int to the file.
	Returns non-zero on success.
**************************************************************/
int	WriteUSHORT( USHORT x, FILE* f )
{
	UCHAR little[ 2 ];	/* BMPs use 16 bit shorts */

	little[ 1 ] = (UCHAR)( ( x & 0xff00 ) >> 8 );
	little[ 0 ] = (UCHAR)( ( x & 0x00ff ) >> 0 );

	return ( f && fwrite( little, 2, 1, f ) == 1 );
};

/**************************************************************
	Reads the BMP file's header into the data structure.
	Returns BMP_OK on success.
**************************************************************/
int	ReadHeader( BMP* bmp, FILE* f )
{
	if ( bmp == NULL || f == NULL )
	{
		return BMP_INVALID_ARGUMENT;
	}

	/* The header's fields are read one by one, and converted from the format's
	little endian to the system's native representation. */
	if ( !ReadUSHORT( &( bmp->Header.Magic ), f ) )			return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.FileSize ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUSHORT( &( bmp->Header.Reserved1 ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUSHORT( &( bmp->Header.Reserved2 ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.DataOffset ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.HeaderSize ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.Width ), f ) )			return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.Height ), f ) )			return BMP_IO_ERROR;
	if ( !ReadUSHORT( &( bmp->Header.Planes ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUSHORT( &( bmp->Header.BitsPerPixel ), f ) )	return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.CompressionType ), f ) )	return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.ImageDataSize ), f ) )	return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.HPixelsPerMeter ), f ) )	return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.VPixelsPerMeter ), f ) )	return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.ColorsUsed ), f ) )		return BMP_IO_ERROR;
	if ( !ReadUINT( &( bmp->Header.ColorsRequired ), f ) )	return BMP_IO_ERROR;

	return BMP_OK;
}

/*********************************** Public methods **********************************/

static const char * QDBMP_probe_data(const u8 *data, u32 size, GF_FilterProbeScore *score)
{
	if ((size >= 54) && (data[0] == 'B') && (data[1] == 'M')) {
		*score = GF_FPROBE_SUPPORTED;
		return "image/bmp";
	}
	return NULL;
}

static GF_Err QDBMP_configure_pid(GF_Filter *filter, GF_FilterPid *pid, Bool is_remove)
{
	const GF_PropertyValue *prop;
	GF_QDBMPCtx *ctx = (GF_QDBMPCtx *)gf_filter_get_udta(filter);

	// disconnect of src pid (not yet supported)
	if (is_remove)
	{
		if (ctx->opid)
		{
			gf_filter_pid_remove(ctx->opid);
			ctx->opid = NULL;
		}
		ctx->ipid = NULL;
		return GF_OK;
	}
	if (!gf_filter_pid_check_caps(pid))
		return GF_NOT_SUPPORTED;

	ctx->ipid = pid;

	if (!ctx->opid)
	{
		ctx->opid = gf_filter_pid_new(filter);
	}
	gf_filter_pid_set_framing_mode(pid, GF_TRUE);

	// copy properties at init or reconfig
	gf_filter_pid_copy_properties(ctx->opid, ctx->ipid);
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_CODECID, &PROP_UINT(GF_CODECID_RAW));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STREAM_TYPE, &PROP_UINT(GF_STREAM_VISUAL));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, & PROP_UINT( GF_PIXEL_RGB ));
	gf_filter_set_name(filter, "QDBMP");

	return GF_OK;
}

static Bool QDBMP_process_event(GF_Filter *filter, const GF_FilterEvent *evt)
{
	GF_FilterEvent fevt;
	GF_QDBMPCtx *ctx = gf_filter_get_udta(filter);
	if (evt->base.on_pid != ctx->opid) return GF_TRUE;
	switch (evt->base.type) {
	case GF_FEVT_PLAY:
		if (ctx->is_playing) {
			return GF_TRUE;
		}

		ctx->is_playing = GF_TRUE;
		if (!ctx->initial_play_done) {
			ctx->initial_play_done = GF_TRUE;
			return GF_TRUE;
		}

		GF_FEVT_INIT(fevt, GF_FEVT_SOURCE_SEEK, ctx->ipid);
		fevt.seek.start_offset = 0;
		gf_filter_pid_send_event(ctx->ipid, &fevt);
		return GF_TRUE;
	case GF_FEVT_STOP:
		ctx->is_playing = GF_FALSE;
		return GF_FALSE;
	default:
		break;
	}
	//cancel all events
	return GF_TRUE;
}

static const GF_FilterCapability QDBMPFullCaps[] =
	{
		CAP_UINT(GF_CAPS_INPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_FILE),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_FILE_EXT, "bmp"),
		CAP_STRING(GF_CAPS_INPUT, GF_PROP_PID_MIME, "image/bmp"),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_STREAM_TYPE, GF_STREAM_VISUAL),
		CAP_UINT(GF_CAPS_OUTPUT, GF_PROP_PID_CODECID, GF_CODECID_RAW),
};

/**************************************************************
	Reads the specified BMP image file.
**************************************************************/
static GF_Err QDBMP_process(GF_Filter *filter)
{
	GF_QDBMPCtx *ctx = gf_filter_get_udta(filter);
	GF_FilterPacket *pck, *dst_pck;
	u8 *data, *output;
	u32 i, j, size;
	BMP*	bmp;
	u32 palettesize;

	pck = gf_filter_pid_get_packet(ctx->ipid);
	if (!pck)
	{
		if (gf_filter_pid_is_eos(ctx->ipid))
		{
			if (ctx->opid)
				gf_filter_pid_set_eos(ctx->opid);
			return GF_EOS;
		}
		return GF_OK;
	}
	data = (unsigned char *)gf_filter_pck_get_data(pck, &size);

	/* Allocate */
	bmp = (BMP*)gf_malloc(sizeof( BMP ) );
	if (bmp == NULL)
	{
		return GF_OUT_OF_MEM;
	}

	FILE* f = fmemopen(data, size,"rb");

	if ( ReadHeader( bmp, f ) != BMP_OK || bmp->Header.Magic != 0x4D42 )
	{
		BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
		fclose( f );
		gf_free( bmp );
		return GF_CORRUPTED_DATA;
	}

	if ( bmp->Header.BitsPerPixel == 8 ) palettesize = BMP_PALETTE_SIZE_8bpp;
	if ( bmp->Header.BitsPerPixel == 4 ) palettesize = BMP_PALETTE_SIZE_4bpp;

	/* Verify that the bitmap variant is supported */
	if ( ( bmp->Header.BitsPerPixel != 32 && bmp->Header.BitsPerPixel != 24
		&& bmp->Header.BitsPerPixel != 8 && bmp->Header.BitsPerPixel != 4 )
		|| bmp->Header.CompressionType != 0 || bmp->Header.HeaderSize != 40 )
	{
		BMP_LAST_ERROR_CODE = BMP_FILE_NOT_SUPPORTED;
		fclose( f );
		gf_free( bmp );
		return GF_NOT_SUPPORTED;
	}

	/* Allocate and read palette */
	if ( palettesize > 0 )
	{
		bmp->Palette = (UCHAR*) malloc( palettesize * sizeof( UCHAR ) );
		if ( bmp->Palette == NULL )
		{
			BMP_LAST_ERROR_CODE = BMP_OUT_OF_MEMORY;
			fclose( f );
			gf_free( bmp );
			return GF_OUT_OF_MEM;
		}

		if ( fread( bmp->Palette, sizeof( UCHAR ), palettesize, f ) != palettesize )
		{
			BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
			fclose( f );
			gf_free( bmp->Palette );
			gf_free( bmp );
			return GF_CORRUPTED_DATA;
		}
	}
	else	/* Not an indexed image */
	{
		bmp->Palette = NULL;
	}

	dst_pck = gf_filter_pck_new_alloc(ctx->opid,  BMP_GetWidth(bmp)*BMP_GetHeight(bmp)*4, &output);

	switch (BMP_GetDepth(bmp)){
		case 32:
		gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, & PROP_UINT( GF_PIXEL_RGBX ));
		fread( output, sizeof( UCHAR ), BMP_GetWidth(bmp)*BMP_GetHeight(bmp)*4, f );
		break;

		case 24:
		return GF_NOT_SUPPORTED; //TODO
		{
			int stride = bmp->Header.Width;
			if (bmp->Header.Width % 4 != 0)
				stride = bmp->Header.Width + (4 - (bmp->Header.Width % 4)); // typically 4-byte aligned
			int diff = bmp->Header.FileSize - (stride * bmp->Header.Height * 3) - palettesize;
			if (diff > 4 || diff < 0)
			{
				printf("error in stride\n");
				return GF_CORRUPTED_DATA;
			} // need to handcheck if not aligned
		}

		break;

		case 8:
		return GF_NOT_SUPPORTED; //TODO
		break;
	}
	/* Allocate memory for image data */
	//dst_pck = gf_filter_pck_new_alloc(ctx->opid,  BMP_GetWidth(bmp)*BMP_GetHeight(bmp)*4, &output);

	//fread( output, sizeof( UCHAR ), bmp->Header.ImageDataSize, f ); // FIXME : size is not matching
	/*if ( fread( output, sizeof( UCHAR ), bmp->Header.ImageDataSize, f ) != bmp->Header.ImageDataSize )
	{
		BMP_LAST_ERROR_CODE = BMP_FILE_INVALID;
		fclose( f );
		gf_free( bmp->Data );
		gf_free( bmp->Palette );
		gf_free( bmp );
		return GF_CORRUPTED_DATA;
	}*/

	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(BMP_GetWidth(bmp)));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(BMP_GetHeight(bmp)));
	gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(4 * BMP_GetWidth(bmp)));

	fclose( f );
	gf_free( bmp->Palette );
	gf_free( bmp );

	gf_filter_pck_merge_properties(pck, dst_pck);
	gf_filter_pck_set_dependency_flags(dst_pck, 0);
	gf_filter_pck_send(dst_pck);
	gf_filter_pid_drop_packet(ctx->ipid);

	// dataInd = 0;
	// /* Read header */
	// if ((ReadHeader(data, size) != BMP_OK) || (bmp->Header.Magic != 0x4D42))
	// {

	// 	free(bmp);
	// 	return GF_NOT_SUPPORTED;
	// }

	// /* Verify that the bitmap variant is supported */
	// if ((bmp->Header.BitsPerPixel != 32 && bmp->Header.BitsPerPixel != 24 && bmp->Header.BitsPerPixel != 8) || bmp->Header.CompressionType != 0 || bmp->Header.HeaderSize != 40)
	// {
	// 	free(bmp);
	// 	return GF_NOT_SUPPORTED;
	// }

	// /* Allocate and read palette */
	// if (bmp->Header.BitsPerPixel == 8)
	// {
	// 	bmp->Palette = (UCHAR *)malloc(BMP_PALETTE_SIZE * sizeof(UCHAR));
	// 	if (bmp->Palette == NULL)
	// 	{
	// 		free(bmp);
	// 		return GF_OUT_OF_MEM;
	// 	}

	// 	if (dataInd + BMP_PALETTE_SIZE > size)
	// 	{
	// 		free(bmp->Palette);
	// 		free(bmp);
	// 		return GF_CORRUPTED_DATA;
	// 	}
	// 	else
	// 	{
	// 		memcpy(bmp->Palette, data + dataInd, BMP_PALETTE_SIZE);
	// 		dataInd += BMP_PALETTE_SIZE;
	// 	}
	// }
	// else /* Not an indexed image */
	// {
	// 	bmp->Palette = NULL;
	// }

	// pck_dst = gf_filter_pck_new_alloc(ctx->opid, bmp->Header.Width * bmp->Header.Height * bpp, &bmp->Data);
	// if (!pck_dst)
	// 	return GF_OUT_OF_MEM;

	// /* Allocate memory for image data */
	// /*bmp->Data = (UCHAR*) malloc( bmp->Header.ImageDataSize );*/
	// // bmp->Data = (UCHAR*) malloc( bmp->Header.Width* bmp->Header.Height * 4); /* forcing RGBA output*/
	// if (bmp->Data == NULL)
	// {
	// 	free(bmp->Palette);
	// 	free(bmp);
	// 	return GF_OUT_OF_MEM;
	// }

	// /* Read image data */
	// if (dataInd + bmp->Header.ImageDataSize > size)
	// {
	// 	free(bmp->Data);
	// 	free(bmp->Palette);
	// 	free(bmp);
	// 	return GF_CORRUPTED_DATA;
	// }
	// // TODO:
	// //  handle encoded
	// //  add in palette. currently 8bpp unmapped
	// else
	// {
	// 	if (bmp->Header.BitsPerPixel == 32) // nothing much to do for this case; unless in BGR format
	// 	{
	// 		// FIXME : Image is upside down :(
	// 		memcpy( bmp->Data, data+dataInd, bmp->Header.Width* bmp->Header.Height * bpp);
	// 	}
	// 	else if (bmp->Header.BitsPerPixel == 24)
	// 	{ /* we need to insert that alpha; rather than memcpy RGB chunks, let's go one-by-one in case we need to debug  */
	// 		int stride = bmp->Header.Width;
	// 		if (bmp->Header.Width % 4 != 0)
	// 			stride = bmp->Header.Width + (4 - (bmp->Header.Width % 4)); // typically 4-byte aligned
	// 		int diff = bmp->Header.FileSize - (stride * bmp->Header.Height * 3) - dataInd;
	// 		if (diff > 4 || diff < 0)
	// 		{
	// 			printf("error in stride\n");
	// 			return GF_CORRUPTED_DATA;
	// 		} // need to handcheck if not aligned

	// 		UCHAR *tmp = bmp->Data;

	// 		// for (i=0; i<bmp->Header.Height; ++i) // if flipped vertically
	// 		for (i = (bmp->Header.Height) - 1; i > -1; --i)
	// 		{
	// 			for (j = 0; j < bmp->Header.Width * 3; j = j + 3)
	// 			{
	// 				// typically stored in BGR so switch to RGB
	// 				if (i < 987)
	// 				{
	// 					unsigned char *ctmp;
	// 					*ctmp = *(data + dataInd + i * stride * 3 + j);
	// 				}
	// 				*(tmp) = *(data + dataInd + i * stride * 3 + j + 2);
	// 				++tmp;
	// 				*(tmp) = *(data + dataInd + i * stride * 3 + j + 1);
	// 				++tmp;
	// 				*(tmp) = *(data + dataInd + i * stride * 3 + j);
	// 				++tmp;
	// 				*(tmp) = 255;
	// 				++tmp;
	// 			}
	// 		}
	// 	}
	// 	else if (bmp->Header.BitsPerPixel == 8)
	// 	{ /* we need to expand to RGB and insert the alpha  */
	// 		UCHAR *tmp = bmp->Data;
	// 		for (i = 0; i < bmp->Header.Height * bmp->Header.Width; ++i)
	// 		{
	// 			*(tmp) = *(data + dataInd + i);
	// 			++tmp;
	// 			*(tmp) = *(data + dataInd + i);
	// 			++tmp;
	// 			*(tmp) = *(data + dataInd + i);
	// 			++tmp;
	// 			*(tmp) = 255;
	// 			++tmp;
	// 		}
	// 	}
	// }
	// // pass on back
	// // Keeping the width/height etc as funcs rahter than just accessing the structure, in case we move this
	// // code elsewhere
	// gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_WIDTH, &PROP_UINT(BMP_GetWidth()));
	// gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_HEIGHT, &PROP_UINT(BMP_GetHeight()));

	// // Only doing RGBA output at the moment
	// gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_PIXFMT, &PROP_UINT(GF_PIXEL_RGBX));
	// gf_filter_pid_set_property(ctx->opid, GF_PROP_PID_STRIDE, &PROP_UINT(bpp * BMP_GetWidth()));

	// gf_filter_pck_merge_properties(pck, pck_dst);
	// gf_filter_pck_set_dependency_flags(pck_dst, 0);
	// gf_filter_pck_send(pck_dst);
	// gf_filter_pid_drop_packet(ctx->ipid);

	// // TODO: free the local data
	// free(bmp);

	return GF_OK;
}

GF_FilterRegister QDBMPRegister = {
	.name = "QDBMP",
	.version = "1.0.0",
	GF_FS_SET_DESCRIPTION("Quick n' Dirty BMP Library")
	GF_FS_SET_HELP("QDBMP (Quick n' Dirty BMP) is a minimalistic C library for handling BMP image files.")
	.private_size = sizeof(GF_QDBMPCtx),
	.priority = 1,
	SETCAPS(QDBMPFullCaps),
	.configure_pid = QDBMP_configure_pid,
	.probe_data = QDBMP_probe_data,
	.process = QDBMP_process,
};

const GF_FilterRegister * EMSCRIPTEN_KEEPALIVE dynCall_QDBMP_register(GF_FilterSession *session)
{
	return &QDBMPRegister;
}
