#define VERSION_STRING "0.0.2"
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#define I2C_SLAVE_FORCE 0x0706
#include "interface/vcos/vcos.h"
#include "bcm_host.h"
#include "interface/mmal/mmal.h"
#include "interface/mmal/mmal_buffer.h"
#include "interface/mmal/mmal_logging.h"
#include "interface/mmal/util/mmal_default_components.h"
#include "interface/mmal/util/mmal_util.h"
#include "interface/mmal/util/mmal_util_params.h"
#include "interface/mmal/util/mmal_connection.h"
#include "RaspiCLI.h"
#include <sys/ioctl.h>
#include "raw_header.h"
#include "histogram.h"
#include <math.h>
#include <inttypes.h>
#define DEFAULT_I2C_DEVICE 0
#define I2C_DEVICE_NAME_LEN 13	// "/dev/i2c-XXX"+NULL

static char i2c_device_name[I2C_DEVICE_NAME_LEN];
struct brcm_raw_header *brcm_header = NULL;

enum bayer_order {
	//Carefully ordered so that an hflip is ^1,
	//and a vflip is ^2.
	BAYER_ORDER_BGGR,
	BAYER_ORDER_GBRG,
	BAYER_ORDER_GRBG,
	BAYER_ORDER_RGGB
};

struct sensor_regs {
	uint16_t reg;
	uint16_t data;
};

struct mode_def
{
	struct sensor_regs *regs;
	int num_regs;
	int width;
	int height;
	MMAL_FOURCC_T encoding;
	enum bayer_order order;
	int native_bit_depth;
	uint8_t image_id;
	uint8_t data_lanes;
	int min_vts;
	int line_time_ns;
	uint32_t timing[5];
	uint32_t term[2];
	int black_level;

	uint16_t skip_bytes;
};

struct sensor_def
{
	char *name;
	struct mode_def *modes;
	int num_modes;
	struct sensor_regs *stop;
	int num_stop_regs;

	uint8_t i2c_addr;		// Device I2C slave address
	int i2c_addressing;		// Length of register address values
	int i2c_data_size;		// Length of register data to write

	//  Detecting the device
	int i2c_ident_length;		// Length of I2C ID register
	uint16_t i2c_ident_reg;		// ID register address
	uint16_t i2c_ident_value;	// ID register value

	// Flip configuration
	uint16_t vflip_reg;		// Register for VFlip
	int vflip_reg_bit;		// Bit in that register for VFlip
	uint16_t hflip_reg;		// Register for HFlip
	int hflip_reg_bit;		// Bit in that register for HFlip
	int flips_dont_change_bayer_order;	// Some sensors do not change the
						// Bayer order by adjusting X/Y starts
						// to compensate.

	uint16_t exposure_reg;
	int exposure_reg_num_bits;

	uint16_t vts_reg;
	int vts_reg_num_bits;

	uint16_t gain_reg;
	int gain_reg_num_bits;
};

int64_t previousTime;
int64_t currentTimeMillis() {
  struct timeval time;
  gettimeofday(&time, NULL);
  int64_t s1 = (int64_t)(time.tv_sec) * 1000;
  int64_t s2 = (time.tv_usec / 1000);
  return s1 + s2;
}

#define NUM_ELEMENTS(a)  (sizeof(a) / sizeof(a[0]))

#include "ov5647_modes.h"
#include "imx219_modes.h"
#include "adv7282m_modes.h"

const struct sensor_def *sensors[] = {
	&ov5647,
	&imx219,
	&adv7282,
	NULL
};

enum {
	CommandHelp,
	CommandMode,
	CommandHFlip,
	CommandVFlip,
	CommandExposure,
	CommandGain,
	CommandOutput,
	CommandWriteHeader,
	CommandTimeout,
	CommandSaveRate,
	CommandBitDepth,
	CommandCameraNum,
	CommandExposureus,
	CommandI2cBus,
	CommandAwbGains,
	CommandRegs,
	CommandHinc,
	CommandVinc,
	CommandFps,
	CommandWidth,
	CommandHeight,
	CommandLeft,
	CommandTop,
	CommandVts,
	CommandLine,
	CommandWriteHeader0,
	CommandWriteHeaderG,
	CommandWriteTimestamps,
	CommandWriteEmpty,
	CommandDecodeMetadata,
	/// Defined by radmonpi++
	CommandThreshold,
	CommandDebug,
	CommandShowTime,
	CommandNToSave,
	CommandMatrix,
	CommandSavePixelHisto
};

static COMMAND_LIST cmdline_commands[] =
{
	{ CommandHelp,		"-help",	"?",  "This help information", 0 },
	{ CommandMode,		"-mode",	"md", "Set sensor mode <mode>", 3 },
	{ CommandHFlip,		"-hflip",	"hf", "Set horizontal flip", 0},
	{ CommandVFlip,		"-vflip",	"vf", "Set vertical flip", 0},
	{ CommandExposure,	"-ss",		"e",  "Set the sensor exposure time (not calibrated units)", 0 },
	{ CommandGain,		"-gain",	"g",  "Set the sensor gain code (not calibrated units)", 0 },
	{ CommandOutput,	"-output",	"o",  "Set the output filename", 0 },
	{ CommandWriteHeader,	"-header",	"hd", "Write the BRCM header to the output file", 0 },
	{ CommandTimeout,	"-timeout",	"t",  "Time (in seconds) before shutting down (if not specified, it runs forever)", 0 },
	{ CommandSaveRate, 	"-saverate",	"sr", "Save every Nth frame", 1 },
	{ CommandBitDepth, 	"-bitdepth",	"b",  "Set output raw bit depth (8, 10, 12 or 16, if not specified, set to sensor native)", 1 },
	{ CommandCameraNum, 	"-cameranum",	"c",  "Set camera number to use (0=CAM0, 1=CAM1).", 1 },
	{ CommandExposureus, 	"-expus",	"eus",  "Set the sensor exposure time in micro seconds.", -1 },
	{ CommandI2cBus, 	"-i2c",	        "y",  "Set the I2C bus to use.", -1 },
	{ CommandAwbGains, 	"-awbgains",	"awbg", "Set the AWB gains to use.", 1 },
	{ CommandRegs,	 	"-regs",	"r",  "Change (current mode) regs", 0 },
	{ CommandHinc,		"-hinc",	"hi", "Set horizontal odd/even inc reg", -1},
	{ CommandVinc,		"-vinc",	"vi", "Set vertical odd/even inc reg", -1},
	{ CommandFps,		"-fps",		"f",  "Set framerate regs", -1},
	{ CommandWidth,		"-width",	"w",  "Set current mode width", -1},
	{ CommandHeight,	"-height",	"h",  "Set current mode height", -1},
	{ CommandLeft,		"-left",	"lt", "Set current mode left", -1},
	{ CommandTop,		"-top",		"tp", "Set current mode top", -1},
	{ CommandWriteHeader0,	"-header0",	"hd0","Sets filename to write the BRCM header to", 0 },
	{ CommandWriteHeaderG,	"-headerg",	"hdg","Sets filename to write the .pgm header to", 0 },
	{ CommandWriteTimestamps,"-tstamps",	"ts", "Sets filename to write timestamps to", 0 },
	{ CommandWriteEmpty,	"-empty",	"emp","Write empty output files", 0 },
	{ CommandDecodeMetadata,	"-metadata",	"m","Decode register metadata", 0 },
	/// Defined by radmonpi++
	{ CommandThreshold, "-threshold", "th", "Set threshold to stop adding pixel to an event. Def: 300", 300},
	{ CommandNToSave, "-ntosave", "n",
		"How many times a pixel should have value greater than threshold in order to save it Def: 5", 5},
	{ CommandShowTime, "-showtime", "showt", "Display the time that takes every action", 0},
	{ CommandDebug, "-debug", "d", "Tool to debug the code", 0},
	{ CommandMatrix, "-matrix", "matrix", "Print the output in a matrix form (useful to take pictures)", 0},
	{ CommandSavePixelHisto, "-pixelhisto", "ph", "Create a file with the histogram of all pixels", 0}
};

static int cmdline_commands_size = sizeof(cmdline_commands) / sizeof(cmdline_commands[0]);

typedef struct pts_node {
	int	idx;
	int64_t  pts;
	struct pts_node *nxt;
} *PTS_NODE_T;

typedef struct {
	int mode;
	int hflip;
	int vflip;
	int exposure;
	int gain;
	char *output;
	int capture;
	int write_header;
	uint32_t timeout;
	int saverate;
	int bit_depth;
	int camera_num;
	int exposure_us;
	int i2c_bus;
	double awb_gains_r;
	double awb_gains_b;
	char *regs;
	int hinc;
	int vinc;
	double fps;
	int width;
	int height;
	int left;
	int top;
	char *write_header0;
	char *write_headerg;
	char *write_timestamps;
	int write_empty;
        PTS_NODE_T ptsa;
        PTS_NODE_T ptso;
        int decodemetadata;
	///Defined by radmonpi++
	uint32_t ntoSave;
	uint16_t thres;
	uint8_t showtime;
	uint8_t debug;
	uint8_t matrix;
	char * savepixelhisto;
	const struct sensor_def *sensor;	// El sensor utilizando
} RASPIRAW_PARAMS_T;

void update_regs(const struct sensor_def *sensor, struct mode_def *mode, int hflip, int vflip, int exposure, int gain);

static int i2c_rd(int fd, uint8_t i2c_addr, uint16_t reg, uint8_t *values, uint32_t n, const struct sensor_def *sensor)
{
	int err;
	uint8_t buf[2] = { reg >> 8, reg & 0xff };
	struct i2c_rdwr_ioctl_data msgset;
	struct i2c_msg msgs[2] = {
		{
			 .addr = i2c_addr,
			 .flags = 0,
			 .len = 2,
			 .buf = buf,
		},
		{
			.addr = i2c_addr,
			.flags = I2C_M_RD,
			.len = n,
			.buf = values,
		},
	};

	if (sensor->i2c_addressing == 1)
	{
		msgs[0].len = 1;
	}
	msgset.msgs = msgs;
	msgset.nmsgs = 2;

	err = ioctl(fd, I2C_RDWR, &msgset);
	//vcos_log_error("Read i2c addr %02X, reg %04X (len %d), value %02X, err %d", i2c_addr, msgs[0].buf[0], msgs[0].len, values[0], err);
	if (err != msgset.nmsgs)
		return -1;

	return 0;
}

const struct sensor_def * probe_sensor(void)
{
	int fd;
	const struct sensor_def **sensor_list = &sensors[0];
	const struct sensor_def *sensor = NULL;

	fd = open(i2c_device_name, O_RDWR);
	if (!fd)
	{
		vcos_log_error("Couldn't open I2C device");
		return NULL;
	}

	while(*sensor_list != NULL)
	{
		uint16_t reg = 0;
		sensor = *sensor_list;
		vcos_log_error("Probing sensor %s on addr %02X", sensor->name, sensor->i2c_addr);
		if (sensor->i2c_ident_length <= 2)
		{
			if (!i2c_rd(fd, sensor->i2c_addr, sensor->i2c_ident_reg, (uint8_t*)&reg, sensor->i2c_ident_length, sensor))
			{
				if (reg == sensor->i2c_ident_value)
				{
					vcos_log_error("Found sensor %s at address %02X", sensor->name, sensor->i2c_addr);
					break;
				}
			}
		}
		sensor_list++;
		sensor = NULL;
	}
	return sensor;
}

void send_regs(int fd, const struct sensor_def *sensor, const struct sensor_regs *regs, int num_regs)
{
	int i;
	for (i=0; i<num_regs; i++)
	{
		if (regs[i].reg == 0xFFFF)
		{
			if (ioctl(fd, I2C_SLAVE_FORCE, regs[i].data) < 0)
			{
				vcos_log_error("Failed to set I2C address to %02X", regs[i].data);
			}
		}
		else if (regs[i].reg == 0xFFFE)
		{
			vcos_sleep(regs[i].data);
		}
		else
		{
			if (sensor->i2c_addressing == 1)
			{
				unsigned char msg[3] = {regs[i].reg, regs[i].data & 0xFF };
				int len = 2;

				if (sensor->i2c_data_size == 2)
				{
					msg[1] = (regs[i].data>>8) & 0xFF;
					msg[2] = regs[i].data & 0xFF;
					len = 3;
				}
				if (write(fd, msg, len) != len)
				{
					vcos_log_error("Failed to write register index %d (%02X val %02X)", i, regs[i].reg, regs[i].data);
				}
			}
			else
			{
				unsigned char msg[4] = {regs[i].reg>>8, regs[i].reg, regs[i].data};
				int len = 3;

				if (sensor->i2c_data_size == 2)
				{
					msg[2] = regs[i].data >> 8;
					msg[3] = regs[i].data;
					len = 4;
				}
				if (write(fd, msg, len) != len)
				{
					vcos_log_error("Failed to write register index %d", i);
				}
			}
		}
	}
}

void start_camera_streaming(const struct sensor_def *sensor, struct mode_def *mode)
{
	int fd;
	fd = open(i2c_device_name, O_RDWR);
	if (!fd)
	{
		vcos_log_error("Couldn't open I2C device");
		return;
	}
	if (ioctl(fd, I2C_SLAVE_FORCE, sensor->i2c_addr) < 0)
	{
		vcos_log_error("Failed to set I2C address");
		return;
	}
	send_regs(fd, sensor, mode->regs, mode->num_regs);
	close(fd);
	vcos_log_error("Now streaming...");
}

void stop_camera_streaming(const struct sensor_def *sensor)
{
	int fd;
	fd = open(i2c_device_name, O_RDWR);
	if (!fd)
	{
		vcos_log_error("Couldn't open I2C device");
		return;
	}
	if (ioctl(fd, I2C_SLAVE_FORCE, sensor->i2c_addr) < 0)
	{
		vcos_log_error("Failed to set I2C address");
		return;
	}
	send_regs(fd, sensor, sensor->stop, sensor->num_stop_regs);
	close(fd);
}

/**
 * Allocates and generates a filename based on the
 * user-supplied pattern and the frame number.
 * On successful return, finalName and tempName point to malloc()ed strings
 * which must be freed externally.  (On failure, returns nulls that
 * don't need free()ing.)
 *
 * @param finalName pointer receives an
 * @param pattern sprintf pattern with %d to be replaced by frame
 * @param frame for timelapse, the frame number
 * @return Returns a MMAL_STATUS_T giving result of operation
*/
MMAL_STATUS_T create_filenames(char** finalName, char * pattern, int frame)
{
	*finalName = NULL;
	if (0 > asprintf(finalName, pattern, frame))
	{
		return MMAL_ENOMEM;    // It may be some other error, but it is not worth getting it right
	}
	return MMAL_SUCCESS;
}

void decodemetadataline(uint8_t *data, int bpp)
{
	int c=1;
	uint8_t tag,dta;
	uint16_t reg=-1;

	if (data[0]==0x0a)
	{

		while (data[c]!=0x07)
		{
			tag=data[c++];
			if ((bpp=10) && c%5==4)
				c++;
			if ((bpp=12) && c%3==2)
				c++;
			dta=data[c++];

			if (tag==0xaa)
				reg=(reg&0x00ff)|(dta<<8);
			else if (tag==0xa5)
				reg=(reg&0xff00)|dta;
			else if (tag==0x5a)
				vcos_log_error("Register 0x%04x = 0x%02x",reg++,dta);
			else if (tag==0x55)
				vcos_log_error("Skip     0x%04x",reg++);
			else
				vcos_log_error("Metadata decode failed %x %x %x",reg,tag,dta);
		}
	}
	else
		vcos_log_error("Doesn't looks like register set %x!=0x0a",data[0]);

}

int encoding_to_bpp(uint32_t encoding)
{
       switch(encoding)
       {
       case    MMAL_ENCODING_BAYER_SBGGR10P:
       case    MMAL_ENCODING_BAYER_SGBRG10P:
       case    MMAL_ENCODING_BAYER_SGRBG10P:
       case    MMAL_ENCODING_BAYER_SRGGB10P:
               return 10;
       case    MMAL_ENCODING_BAYER_SBGGR12P:
       case    MMAL_ENCODING_BAYER_SGBRG12P:
       case    MMAL_ENCODING_BAYER_SGRBG12P:
       case    MMAL_ENCODING_BAYER_SRGGB12P:
               return 12;
       default:
               return 8;
       };

}


///	FIXME: Al parecer esta función está deprecada.
///    No se encuentra en ningún otro lado en el código.
///    LOL!
void unpackedAndSave(MMAL_BUFFER_HEADER_T *buffer, char *filename_up, RASPIRAW_PARAMS_T * cfg)
{
	FILE *file;
	file = fopen(filename_up, "w");
	if (cfg->debug) fprintf(stderr, "Tratando de imprimir\n");
	if(file)
	{
		uint16_t pixel = 0;
		uint32_t max_buffer_length;
		int i;
		int j;
		// Maximo multiplo de 5 menor que buffer->length
		// para asegurar que exista el 5to byte
		max_buffer_length = (buffer->length-3264*8)/5*5;
		// Se eliminan las ultimas 8 filas ya que no tiene informacion util (-3264*8)
		for (i=0; i<max_buffer_length; i++)
		{
			// j:= indice que apunta a cada elemento de una fila
			// Se reinicia a cero al comenzar una nueva fila
			j = i%3264;
			// Descartamos los 24 bytes del final de cada fila con j<3240 (why?)
			if (!((j+1)%5)==0 && j<3240)
			{
				// El 5to byte tiene informacion de los pixeles anteriores, pero no se usa
				pixel = (uint8_t)buffer->data[i] << 2;			// Pongo el primer byte, dejando dos bits libres para
				pixel += (uint8_t)buffer->data[i+4-j%5] >> 2*(3-j%5)&0x3;	// Pongo los otros dos bytes, completanto el número
				//fwrite(&pixel, sizeof(pixel), 1, file);     	// Binary writing
				fprintf(file, "%u\t", pixel); 					// Text writing
			}
			if (i!=0 && j==0) fprintf(file, "\n");
		}
		fclose(file);
	}
}

double mean_value_skiping(const uint16_t *imagen_actual, RASPIRAW_PARAMS_T * cfg,unsigned int skiping){
	int i=0;
	uint32_t len=cfg->width*cfg->height;
	uint64_t sum= 0;
	if (!skiping){
		for(i=0;i<len;i++)
			sum += imagen_actual[i];
	} else {
		for(i=0;i<len;i+=skiping+1)
			sum += imagen_actual[i];
	}
	return sum*(double)(skiping+1)/len;
}

double mean_value(const uint16_t *imagen_actual, RASPIRAW_PARAMS_T * cfg){
	return mean_value_skiping(imagen_actual,cfg,0);
}

uint16_t *unpackedAndCopy(MMAL_BUFFER_HEADER_T *buffer, RASPIRAW_PARAMS_T * cfg)
{
	int i,k=0;
	uint16_t *img;
	img = (uint16_t *) malloc(cfg->width*cfg->height*sizeof(uint16_t));
	if (img==NULL)	{
		fprintf(stderr,"No se pudo allocar memoria\n");
		return NULL;
	}
	
	if (cfg->height*cfg->width*cfg->sensor->modes[cfg->mode].native_bit_depth+cfg->width*cfg->sensor->modes[cfg->mode].skip_bytes < buffer->length) {
		fprintf(stderr, "The buffer isn't long enough\n");
		return NULL;
	}

	i=0;
	int lastbyte = 0;
	uint32_t l =0;
	uint32_t m =0;
	uint32_t img_len = cfg->width*cfg->height;
	uint32_t width = cfg->width;
	//uint32_t height = cfg->height;
	uint32_t skip_bytes = cfg->sensor->modes[cfg->mode].skip_bytes;
	uint32_t row_size = cfg->width*cfg->sensor->modes[cfg->mode].native_bit_depth/8;
	uint32_t row_buffer_size = row_size + cfg->sensor->modes[cfg->mode].skip_bytes;
	uint8_t * data = buffer->data;
	// FIXME: Asumo que será:	AAAA AAAA BBBB BBBB CCCC CCCC DDDD DDDD AABB CCDD
	if (cfg->sensor->modes[cfg->mode].native_bit_depth == 10 && cfg->bit_depth == 10)
	{
		for(l=0;l<img_len;l+=width) {
			for(m=0;m<width;i++) {
				lastbyte= i+4;
				img[l+m++] = (data[i] << 2) + (data[lastbyte] >> 6&0x3);i++;
				img[l+m++] = (data[i] << 2) + (data[lastbyte] >> 4&0x3);i++;
				img[l+m++] = (data[i] << 2) + (data[lastbyte] >> 2&0x3);i++;
				img[l+m++] = (data[i] << 2) + (data[lastbyte] &0x3);	i++;
			}
			i+=skip_bytes;
		}
	}
	else if (cfg->sensor->modes[cfg->mode].native_bit_depth == 10 && cfg->bit_depth == 8)
	{
		//FIXME: Al parecer la señal está modulada, pero como no lo voy a usar, después me preocupo por esto.
		while (k < cfg->width*cfg->height) {
			if ((i% row_buffer_size) == row_size)	{
				i+=cfg->sensor->modes[cfg->mode].skip_bytes;
				continue;
			}
			img[k++] = (uint8_t)buffer->data[i++];
			img[k++] = (uint8_t)buffer->data[i++];
			img[k++] = (uint8_t)buffer->data[i++];
			img[k++] = (uint8_t)buffer->data[i++];
			i++;
		}
	}
	else
	{
		fprintf(stderr,"FIXME: bit_depth != 10 isn't supported yet");
		return NULL;
	}
	return img;
}

uint32_t *imagen_mask = NULL;
int contador_imagenes = 0;

typedef struct {
	int x;
	int y;
	int value;
} coord_t;

#define MAX_SIZE cfg->width*cfg->height // Maximum size of an event (I wanted to avoid lists)

int running = 0;
uint16_t *imagen_actual = NULL;
uint32_t *pixel_counter;
uint64_t *sum_value;
uint64_t time_first_call;
uint64_t time_last_call = 0;
Histogram_t pixel_histo = DEFAULT_HISTO;
static void callback(MMAL_PORT_T *port, MMAL_BUFFER_HEADER_T *buffer)
{
	static int count = 0;
	//vcos_log_error("Buffer %p returned, filled %d, timestamp %llu, flags %04X", buffer, buffer->length, buffer->pts, buffer->flags);
	if (running)
	{
		RASPIRAW_PARAMS_T *cfg = (RASPIRAW_PARAMS_T *)port->userdata;

		if (!(buffer->flags&MMAL_BUFFER_HEADER_FLAG_CODECSIDEINFO) &&
                    (((count++)%cfg->saverate)==0))
		{
			// Save every Nth frame
			// SD card access is too slow to do much more.
			FILE *file;
			char *filename = NULL;
			if (cfg->capture == 1 && create_filenames(&filename, cfg->output, count) == MMAL_SUCCESS)
			{
				file = fopen(filename, "wb");
				if (file)
				{
					if (cfg->ptso)  // make sure previous malloc() was successful
					{
						cfg->ptso->idx = count;
						cfg->ptso->pts = buffer->pts;
						cfg->ptso->nxt = malloc(sizeof(*cfg->ptso->nxt));
						cfg->ptso = cfg->ptso->nxt;
					}
					if (!cfg->write_empty)
					{
						if (cfg->write_header)
							fwrite(brcm_header, BRCM_RAW_HEADER_LENGTH, 1, file);
						fwrite(buffer->data, buffer->length, 1, file);
						fprintf(stderr, "Raw image saved to %s\n", filename);
					}
					fclose(file);
				}
				free(filename);
			}
				
			if (cfg->debug) fprintf(stderr,"Foto %i\t", contador_imagenes);		// Ex - Begin clara
			
			// Descartar los primeros frames (A veces tienen basura)
			if(contador_imagenes == 3)
				time_first_call = currentTimeMillis();
			if(contador_imagenes > 2){
				if (cfg->showtime)
					if (time_last_call!=0)
						fprintf(stderr, "Total time:\t%Lu ms\n",currentTimeMillis() - time_last_call);
				time_last_call = currentTimeMillis();

				if (cfg->showtime) previousTime = currentTimeMillis();
				imagen_actual = unpackedAndCopy(buffer,cfg);
				if (cfg->showtime) fprintf(stderr, "\tIt took me\t%Lu ms\tto copy an image\n", currentTimeMillis() - previousTime);

				int i;
				if (cfg->showtime) previousTime = currentTimeMillis();

				if (cfg->savepixelhisto)
					for(i=0;i<cfg->width*cfg->height;i++) {
						if (imagen_actual[i]>=cfg->thres) {
							pixel_counter[i]++;
							sum_value[i] += imagen_actual[i];
						}
						histo_add(&pixel_histo,imagen_actual[i]);
					}
				else
					for(i=0;i<cfg->width*cfg->height;i++)
						if (imagen_actual[i]>=cfg->thres) {
							pixel_counter[i]++;
							sum_value[i] += imagen_actual[i];
						}

				if (cfg->showtime) fprintf(stderr, "\tIt took me\t%Lu ms\tto process an image\n", currentTimeMillis() - previousTime);
				
				if (cfg->debug){
					uint32_t pixel_rand = rand()%(cfg->width*cfg->height);
					fprintf(stderr, "\timg[%u,%u]=%u\n",pixel_rand%cfg->width, pixel_rand/cfg->width, imagen_actual[pixel_rand]);
				}

				if (cfg->debug >= 2){
					fprintf(stderr, "\tMean Value: %lf\n",mean_value(imagen_actual, cfg));
					if (cfg->debug >=3 && cfg->showtime){
						previousTime = currentTimeMillis();
						int j;
						for(j=0; j<cfg->width*cfg->height; j++);
						fprintf(stderr, "\tIt took me\t%Lu ms\tto make a dummy for\n",currentTimeMillis() - previousTime);
					}
				}

				free(imagen_actual);
			}
			contador_imagenes++;
		}

		if (cfg->decodemetadata && (buffer->flags&MMAL_BUFFER_HEADER_FLAG_CODECSIDEINFO))
		{
			int bpp = encoding_to_bpp(port->format->encoding);
			vcos_log_error("First metadata line");
			decodemetadataline(buffer->data, bpp);
			vcos_log_error("Second metadata line");
			decodemetadataline(buffer->data+VCOS_ALIGN_UP(5*(port->format->es->video.width/4),16), bpp);
		}

		buffer->length = 0;
		mmal_port_send_buffer(port, buffer);
	}
	else
		mmal_buffer_header_release(buffer);
}

uint32_t order_and_bit_depth_to_encoding(enum bayer_order order, int bit_depth)
{
	//BAYER_ORDER_BGGR,
	//BAYER_ORDER_GBRG,
	//BAYER_ORDER_GRBG,
	//BAYER_ORDER_RGGB
	const uint32_t depth8[] = {
		MMAL_ENCODING_BAYER_SBGGR8,
		MMAL_ENCODING_BAYER_SGBRG8,
		MMAL_ENCODING_BAYER_SGRBG8,
		MMAL_ENCODING_BAYER_SRGGB8
	};
	const uint32_t depth10[] = {
		MMAL_ENCODING_BAYER_SBGGR10P,
		MMAL_ENCODING_BAYER_SGBRG10P,
		MMAL_ENCODING_BAYER_SGRBG10P,
		MMAL_ENCODING_BAYER_SRGGB10P
	};
	const uint32_t depth12[] = {
		MMAL_ENCODING_BAYER_SBGGR12P,
		MMAL_ENCODING_BAYER_SGBRG12P,
		MMAL_ENCODING_BAYER_SGRBG12P,
		MMAL_ENCODING_BAYER_SRGGB12P,
	};
	const uint32_t depth16[] = {
		MMAL_ENCODING_BAYER_SBGGR16,
		MMAL_ENCODING_BAYER_SGBRG16,
		MMAL_ENCODING_BAYER_SGRBG16,
		MMAL_ENCODING_BAYER_SRGGB16,
	};
	if (order < 0 || order > 3)
	{
		vcos_log_error("order out of range - %d", order);
		return 0;
	}

	switch(bit_depth)
	{
		case 8:
			return depth8[order];
		case 10:
			return depth10[order];
		case 12:
			return depth12[order];
		case 16:
			return depth16[order];
	}
	vcos_log_error("%d not one of the handled bit depths", bit_depth);
	return 0;
}

/**
 * Parse the incoming command line and put resulting parameters in to the state
 *
 * @param argc Number of arguments in command line
 * @param argv Array of pointers to strings from command line
 * @param state Pointer to state structure to assign any discovered parameters to
 * @return non-0 if failed for some reason, 0 otherwise
 */
static int parse_cmdline(int argc, char **argv, RASPIRAW_PARAMS_T *cfg)
{
	// Parse the command line arguments.
	// We are looking for --<something> or -<abbreviation of something>

	int valid = 1;
	int i;

	for (i = 1; i < argc && valid; i++)
	{
		int command_id, num_parameters, len;

		if (!argv[i])
			continue;

		if (argv[i][0] != '-')
		{
			valid = 0;
			continue;
		}

		// Assume parameter is valid until proven otherwise
		valid = 1;

		command_id = raspicli_get_command_id(cmdline_commands, cmdline_commands_size, &argv[i][1], &num_parameters);

		// If we found a command but are missing a parameter, continue (and we will drop out of the loop)
		if (command_id != -1 && num_parameters > 0 && (i + 1 >= argc) )
			continue;

		// We are now dealing with a command line option
		switch (command_id)
		{
			case CommandHelp:
				raspicli_display_help(cmdline_commands, cmdline_commands_size);
				// exit straight away if help requested
				return -1;

			case CommandMode:
				if (sscanf(argv[i + 1], "%d", &cfg->mode) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandHFlip:
				cfg->hflip = 1;
				break;

			case CommandVFlip:
				cfg->vflip = 1;
				break;

			case CommandExposure:
				if (sscanf(argv[i + 1], "%d", &cfg->exposure) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandGain:
				if (sscanf(argv[i + 1], "%d", &cfg->gain) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandOutput:  // output filename
			{
				len = strlen(argv[i + 1]);
				if (len)
				{
					//We use sprintf to append the frame number for timelapse mode
					//Ensure that any %<char> is either %% or %d.
					const char *percent = argv[i+1];
					while(valid && *percent && (percent=strchr(percent, '%')) != NULL)
					{
						int digits=0;
						percent++;
						while(isdigit(*percent))
						{
							percent++;
							digits++;
						}
						if (!((*percent == '%' && !digits) || *percent == 'd'))
						{
							valid = 0;
							fprintf(stderr, "Filename contains %% characters, but not %%d or %%%% - sorry, will fail\n");
						}
						percent++;
					}
				}
				cfg->output = malloc(len + 10); // leave enough space for any timelapse generated changes to filename
				vcos_assert(cfg->output);
				if (cfg->output) {
					strncpy(cfg->output, argv[i + 1], len+1);
					i++;
					cfg->capture = 1;
				}
				else
					valid = 0;
				break;
			}

			case CommandWriteHeader:
				cfg->write_header = 1;
				break;

			case CommandTimeout: // Time to run for in milliseconds
				if (sscanf(argv[i + 1], "%u", &cfg->timeout) == 1)
				{
					i++;
				}
				else
					valid = 0;
				break;

			case CommandSaveRate:
				if (sscanf(argv[i + 1], "%u", &cfg->saverate) == 1)
				{
					i++;
				}
				else
					valid = 0;
				break;

			case CommandSavePixelHisto:
				cfg->savepixelhisto = argv[i + 1];
				i++;
				break;


			case CommandBitDepth:
				if (sscanf(argv[i + 1], "%u", &cfg->bit_depth) == 1)
				{
					i++;
				}
				else
					valid = 0;
				break;

			case CommandCameraNum:
				if (sscanf(argv[i + 1], "%u", &cfg->camera_num) == 1)
				{
					i++;
					if (cfg->camera_num !=0 && cfg->camera_num != 1)
					{
						fprintf(stderr, "Invalid camera number specified (%d)."
							" It should be 0 or 1.\n", cfg->camera_num);
						valid = 0;
					}
				}
				else
					valid = 0;
				break;

			case CommandExposureus:
				if (sscanf(argv[i + 1], "%d", &cfg->exposure_us) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandI2cBus:
				if (sscanf(argv[i + 1], "%d", &cfg->i2c_bus) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandAwbGains:
			{
				double r,b;
				int args;

				args = sscanf(argv[i + 1], "%lf,%lf", &r,&b);

				if (args != 2 || r > 8.0 || b > 8.0)
					valid = 0;

				cfg->awb_gains_r = r;
				cfg->awb_gains_b = b;

				i++;
				break;
			}

			case CommandRegs:  // register changes
			{
				len = strlen(argv[i + 1]);
				cfg->regs = malloc(len+1);
				vcos_assert(cfg->regs);
				strncpy(cfg->regs, argv[i + 1], len+1);
				i++;
				break;
			}

			case CommandHinc:
				if (strlen(argv[i+1]) != 2 ||
                                    sscanf(argv[i + 1], "%x", &cfg->hinc) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandVinc:
				if (strlen(argv[i+1]) != 2 ||
                                    sscanf(argv[i + 1], "%x", &cfg->vinc) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandFps:
                                if (sscanf(argv[i + 1], "%lf", &cfg->fps) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandWidth:
				if (sscanf(argv[i + 1], "%d", &cfg->width) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandHeight:
				if (sscanf(argv[i + 1], "%d", &cfg->height) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandLeft:
				if (sscanf(argv[i + 1], "%d", &cfg->left) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandTop:
				if (sscanf(argv[i + 1], "%d", &cfg->top) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandWriteHeader0:
				len = strlen(argv[i + 1]);
				cfg->write_header0 = malloc(len + 1);
				vcos_assert(cfg->write_header0);
				strncpy(cfg->write_header0, argv[i + 1], len+1);
				i++;
				break;

			case CommandWriteHeaderG:
				len = strlen(argv[i + 1]);
				cfg->write_headerg = malloc(len + 1);
				vcos_assert(cfg->write_headerg);
				strncpy(cfg->write_headerg, argv[i + 1], len+1);
				i++;
				break;

			case CommandWriteTimestamps:
				len = strlen(argv[i + 1]);
				cfg->write_timestamps = malloc(len + 1);
				vcos_assert(cfg->write_timestamps);
				strncpy(cfg->write_timestamps, argv[i + 1], len+1);
				i++;
				cfg->ptsa = malloc(sizeof(*cfg->ptsa));
				cfg->ptso = cfg->ptsa;
				break;

			case CommandWriteEmpty:
				cfg->write_empty = 1;
				break;

			case CommandDecodeMetadata:
				cfg->decodemetadata = 1;
				break;

			case CommandNToSave:
			if (sscanf(argv[i + 1], "%u", &cfg->ntoSave) != 1)
					valid = 0;
				else
					i++;
				break;
			
			case CommandThreshold:
				if (sscanf(argv[i + 1], "%" SCNd16, &cfg->thres) != 1)
					valid = 0;
				else
					i++;
				break;
			
			case CommandDebug:
				if (sscanf(argv[i + 1], "%" SCNu8, &cfg->debug) != 1)
					valid = 0;
				else
					i++;
				break;

			case CommandShowTime:
				//if (sscanf(argv[i + 1], "%" SCNu8, &cfg->showtime) != 1)
					cfg->showtime = 1;
				//	valid = 0;
				//else
				//	i++;
				break;

			case CommandMatrix:
				cfg->matrix = 1;
				break;

			default:
				valid = 0;
				break;
		}
	}

	if (!valid)
	{
		fprintf(stderr, "Invalid command line option (%s)\n", argv[i-1]);
		return 1;
	}

	return 0;
}

//The process first loads the cleaned up dump of the registers
//than updates the known registers to the proper values
//based on: http://www.seeedstudio.com/wiki/images/3/3c/Ov5647_full.pdf
enum operation {
       EQUAL,  //Set bit to value
       SET,    //Set bit
       CLEAR,  //Clear bit
       XOR     //Xor bit
};

void modReg(struct mode_def *mode, uint16_t reg, int startBit, int endBit, int value, enum operation op);

RASPIRAW_PARAMS_T cfg = { 0 };
void INThandler(int sig)
{
	int i;
	uint64_t nBadPixels = 0;
	for (i=0; i<cfg.height*cfg.width; i++)
		if (pixel_counter[i]>=cfg.ntoSave)
			nBadPixels++;

	fprintf(stderr, "Found\t%" PRId64 "\tbad pixels\n",nBadPixels);

	if (cfg.matrix){
		int j;
		printf("P2\n%u\t%u\n%u\n",cfg.width,cfg.height,(1<<cfg.bit_depth)-1);
		for(i=0;i<cfg.height;i++) {
			for(j=0;j<cfg.width-1;j++) {
				printf("%"PRId64"\t",sum_value[j+i*cfg.width]/pixel_counter[j+i*cfg.width]);
			}
			printf("%"PRId64"\n",sum_value[j+i*cfg.width]/pixel_counter[j+i*cfg.width]);
		}
	} else {
		for (i=0; i<cfg.height*cfg.width; i++)
			if (pixel_counter[i]>=cfg.ntoSave)
				printf("\n%u\t%u\t%u\t%"PRId64 ,i%cfg.width, i/cfg.width, pixel_counter[i],sum_value[i]/pixel_counter[i]);
		
	}
	fflush(stdout);
	free(sum_value);
	free(pixel_counter);
	
	if (time_last_call != 0)
		fprintf(stderr, "Total exposure time: %Lu ms\n", time_last_call - time_first_call);
	else
		fprintf(stderr, "WARNING: No image were taken\n");
	
	exit(0);
}

int main(int argc, char** argv) {
	uint32_t encoding;
	const struct sensor_def *sensor;
	struct mode_def *sensor_mode = NULL;

	//Initialise any non-zero config values.
	cfg.mode = 8;
	cfg.exposure = -1;
	cfg.gain = -1;
	cfg.timeout = 0;
	cfg.saverate = 1;
	cfg.bit_depth = -1;
	cfg.camera_num = -1;
	cfg.exposure_us = -1;
	cfg.i2c_bus = DEFAULT_I2C_DEVICE;
	cfg.hinc = -1;
	cfg.vinc = -1;
	cfg.fps = -1;
	cfg.width = -1;
	cfg.height = -1;
	cfg.left = -1;
	cfg.top = -1;
	cfg.thres = 300;
	cfg.ntoSave = 5;
	cfg.debug = 0;
	cfg.showtime = 0;
	cfg.matrix = 0;
	cfg.savepixelhisto = NULL;
	//char default_regs[] = "380C,1F;380D,FF;380E,7F;380F,0F;3500,0F;3501,FF;3502,FF;3036,50;4000,00;5000,00";
	//cfg.regs = default_regs;

	imagen_actual = NULL;

	signal(SIGINT, INThandler);

	bcm_host_init();
	vcos_log_register("RaspiRaw", VCOS_LOG_CATEGORY);

	if (argc == 1)
	{
		fprintf(stderr, "\n%s Camera App %s\n\n", basename(argv[0]), VERSION_STRING);

		raspicli_display_help(cmdline_commands, cmdline_commands_size);
		exit(-1);
	}

	// Parse the command line and put options in to our status structure
	if (parse_cmdline(argc, argv, &cfg))
	{
		exit(-1);
	}

	snprintf(i2c_device_name, sizeof(i2c_device_name), "/dev/i2c-%d", cfg.i2c_bus);
	fprintf(stderr, "Using i2C device %s\n", i2c_device_name);

	sensor = probe_sensor();
	if (!sensor)
	{
		vcos_log_error("No sensor found. Aborting");
		return -1;
	}
	cfg.sensor = sensor;

	if (cfg.mode >= 0 && cfg.mode < sensor->num_modes)
	{
		sensor_mode = &sensor->modes[cfg.mode];
	}

	if (!sensor_mode)
	{
		vcos_log_error("Invalid mode %d - aborting", cfg.mode);
		return -2;
	}


	if (cfg.regs)
	{
		int r,b;
		char *p,*q;

		p=strtok(cfg.regs, ";");
		while (p)
		{
			vcos_assert(strlen(p)>6);
			vcos_assert(p[4]==',');
			vcos_assert(strlen(p)%2);
			p[4]='\0'; q=p+5;
			sscanf(p,"%4x",&r);
			while(*q)
			{
				vcos_assert(isxdigit(q[0]));
				vcos_assert(isxdigit(q[1]));

				sscanf(q,"%2x",&b);
				vcos_log_error("%04x: %02x",r,b);

				modReg(sensor_mode, r, 0, 7, b, EQUAL);

				++r;
				q+=2;
			}
			p=strtok(NULL,";");
		}
	}

	if (cfg.hinc >= 0)
	{
		// TO_DO: handle modes different to ov5647 as well
		modReg(sensor_mode, 0x3814, 0, 7, cfg.hinc, EQUAL);
	}

	if (cfg.vinc >= 0)
	{
		// TO_DO: handle modes different to ov5647 as well
		modReg(sensor_mode, 0x3815, 0, 7, cfg.vinc, EQUAL);
	}

	if (cfg.fps > 0)
	{
		int n = 1000000000 / (sensor_mode->line_time_ns * cfg.fps);
		modReg(sensor_mode, sensor->vts_reg+0, 0, 7, n>>8, EQUAL);
		modReg(sensor_mode, sensor->vts_reg+1, 0, 7, n&0xFF, EQUAL);
	}

	if (cfg.width > 0)
	{
		sensor_mode->width = cfg.width;
		// TO_DO: handle modes different to ov5647 as well
		modReg(sensor_mode, 0x3808, 0, 3, cfg.width >>8, EQUAL);
		modReg(sensor_mode, 0x3809, 0, 7, cfg.width &0xFF, EQUAL);
	} else {
		cfg.width = sensor_mode->width;
	}

	if (cfg.height > 0)
	{
		sensor_mode->height = cfg.height;
		// TO_DO: handle modes different to ov5647 as well
		modReg(sensor_mode, 0x380A, 0, 3, cfg.height >>8, EQUAL);
		modReg(sensor_mode, 0x380B, 0, 7, cfg.height &0xFF, EQUAL);
	} else {
		cfg.height = sensor_mode->height;
	}

	if (cfg.left > 0)
	{
		// TO_DO: handle modes different to ov5647 as well
		int val = cfg.left * (cfg.mode < 2 ? 1 : 1 << (cfg.mode / 2 - 1));
		modReg(sensor_mode, 0x3800, 0, 3, val >>8, EQUAL);
		modReg(sensor_mode, 0x3801, 0, 7, val &0xFF, EQUAL);
	}

	if (cfg.top > 0)
	{
		// TO_DO: handle modes different to ov5647 as well
		int val = cfg.top * (cfg.mode < 2 ? 1 : 1 << (cfg.mode / 2 - 1));
		modReg(sensor_mode, 0x3802, 0, 3, val >>8, EQUAL);
		modReg(sensor_mode, 0x3803, 0, 7, val &0xFF, EQUAL);
	}


	if (cfg.bit_depth == -1)
	{
		cfg.bit_depth = sensor_mode->native_bit_depth;
	}


	if (cfg.write_headerg && (cfg.bit_depth != sensor_mode->native_bit_depth))
	{
		// needs change after fix for https://github.com/6by9/raspiraw/issues/2
		vcos_log_error("--headerG supported for native bit depth only");
		exit(-1);
	}

	if (cfg.exposure_us != -1)
	{
		cfg.exposure = ((int64_t)cfg.exposure_us * 1000) / sensor_mode->line_time_ns;
		vcos_log_error("Setting exposure to %d from time %dus", cfg.exposure, cfg.exposure_us);
	}

	update_regs(sensor, sensor_mode, cfg.hflip, cfg.vflip, cfg.exposure, cfg.gain);
	if (sensor_mode->encoding == 0)
		// Antes estaba en cfg.bit_depth, pero en realidad los datos de la imagen siguen estando igual, so... mejor usar el nativo.ç
		encoding = order_and_bit_depth_to_encoding(sensor_mode->order, sensor_mode->native_bit_depth);
	else
		encoding = sensor_mode->encoding;
	if (!encoding)
	{
		vcos_log_error("Failed to map bitdepth %d and order %d into encoding\n", cfg.bit_depth, sensor_mode->order);
		return -3;
	}
	vcos_log_error("Encoding %08X", encoding);

	MMAL_COMPONENT_T *rawcam=NULL, *isp=NULL, *render=NULL;
	MMAL_STATUS_T status;
	MMAL_PORT_T *output = NULL;
	MMAL_POOL_T *pool = NULL;
	MMAL_CONNECTION_T *rawcam_isp = NULL;
	MMAL_CONNECTION_T *isp_render = NULL;
	MMAL_PARAMETER_CAMERA_RX_CONFIG_T rx_cfg = {{MMAL_PARAMETER_CAMERA_RX_CONFIG, sizeof(rx_cfg)}};
	MMAL_PARAMETER_CAMERA_RX_TIMING_T rx_timing = {{MMAL_PARAMETER_CAMERA_RX_TIMING, sizeof(rx_timing)}};
	int i;

	bcm_host_init();
	vcos_log_register("RaspiRaw", VCOS_LOG_CATEGORY);

	status = mmal_component_create("vc.ril.rawcam", &rawcam);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to create rawcam");
		return -1;
	}

	status = mmal_component_create("vc.ril.isp", &isp);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to create isp");
		goto component_destroy;
	}

	status = mmal_component_create(MMAL_COMPONENT_DEFAULT_VIDEO_RENDERER, &render);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to create render");
		goto component_destroy;
	}

	output = rawcam->output[0];
	status = mmal_port_parameter_get(output, &rx_cfg.hdr);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to get cfg");
		goto component_destroy;
	}
	if (sensor_mode->encoding || cfg.bit_depth == sensor_mode->native_bit_depth)
	{
		rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_NONE;
		rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_NONE;
	}
	else
	{
		switch(sensor_mode->native_bit_depth)
		{
			case 8:
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_8;
				break;
			case 10:
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_10;
				break;
			case 12:
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_12;
				break;
			case 14:
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_16;
				break;
			case 16:
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_16;
				break;
			default:
				vcos_log_error("Unknown native bit depth %d", sensor_mode->native_bit_depth);
				rx_cfg.unpack = MMAL_CAMERA_RX_CONFIG_UNPACK_NONE;
				break;
		}
		switch(cfg.bit_depth)
		{
			case 8:
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_8;
				break;
			case 10:
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_RAW10;
				break;
			case 12:
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_RAW12;
				break;
			case 14:
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_14;
				break;
			case 16:
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_PACK_16;
				break;
			default:
				vcos_log_error("Unknown output bit depth %d", cfg.bit_depth);
				rx_cfg.pack = MMAL_CAMERA_RX_CONFIG_UNPACK_NONE;
				break;
		}
	}
	vcos_log_error("Set pack to %d, unpack to %d", rx_cfg.unpack, rx_cfg.pack);
	if (sensor_mode->data_lanes)
		rx_cfg.data_lanes = sensor_mode->data_lanes;
	if (sensor_mode->image_id)
		rx_cfg.image_id = sensor_mode->image_id;
	status = mmal_port_parameter_set(output, &rx_cfg.hdr);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to set cfg");
		goto component_destroy;
	}
	status = mmal_port_parameter_get(output, &rx_timing.hdr);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to get timing");
		goto component_destroy;
	}
	if (sensor_mode->timing[0])
		rx_timing.timing1 = sensor_mode->timing[0];
	if (sensor_mode->timing[1])
		rx_timing.timing2 = sensor_mode->timing[1];
	if (sensor_mode->timing[2])
		rx_timing.timing3 = sensor_mode->timing[2];
	if (sensor_mode->timing[3])
		rx_timing.timing4 = sensor_mode->timing[3];
	if (sensor_mode->timing[4])
		rx_timing.timing5 = sensor_mode->timing[4];
	if (sensor_mode->term[0])
		rx_timing.term1 = sensor_mode->term[0];
	if (sensor_mode->term[1])
		rx_timing.term2 = sensor_mode->term[1];
	vcos_log_error("Timing %u/%u, %u/%u/%u, %u/%u",
		rx_timing.timing1, rx_timing.timing2,
		rx_timing.timing3, rx_timing.timing4, rx_timing.timing5,
		rx_timing.term1,  rx_timing.term2);
	status = mmal_port_parameter_set(output, &rx_timing.hdr);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to set timing");
		goto component_destroy;
	}

	if (cfg.camera_num != -1) {
		vcos_log_error("Set camera_num to %d", cfg.camera_num);
		status = mmal_port_parameter_set_int32(output, MMAL_PARAMETER_CAMERA_NUM, cfg.camera_num);
		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("Failed to set camera_num");
			goto component_destroy;
		}
	}

	status = mmal_component_enable(rawcam);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to enable rawcam");
		goto component_destroy;
	}
	status = mmal_component_enable(isp);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to enable isp");
		goto component_destroy;
	}
	status = mmal_component_enable(render);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to enable render");
		goto component_destroy;
	}

	output->format->es->video.crop.width = sensor_mode->width;
	output->format->es->video.crop.height = sensor_mode->height;
	output->format->es->video.width = VCOS_ALIGN_UP(sensor_mode->width, 16);
	output->format->es->video.height = VCOS_ALIGN_UP(sensor_mode->height, 16);
	output->format->encoding = encoding;

	status = mmal_port_format_commit(output);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed port_format_commit");
		goto component_disable;
	}

	output->buffer_size = output->buffer_size_recommended;
	output->buffer_num = output->buffer_num_recommended;

	if (1) //cfg.capture) //FAB: Always enter this if()
	{
		if (cfg.write_header || cfg.write_header0)
		{
			brcm_header = (struct brcm_raw_header*)malloc(BRCM_RAW_HEADER_LENGTH);
			if (brcm_header)
			{
				memset(brcm_header, 0, BRCM_RAW_HEADER_LENGTH);
				brcm_header->id = BRCM_ID_SIG;
				brcm_header->version = HEADER_VERSION;
				brcm_header->mode.width = sensor_mode->width;
				brcm_header->mode.height = sensor_mode->height;
				//FIXME: Ought to check that the sensor is producing
				//Bayer rather than just assuming.
				brcm_header->mode.format = VC_IMAGE_BAYER;
				switch(sensor_mode->order)
				{
					case BAYER_ORDER_BGGR:
						brcm_header->mode.bayer_order = VC_IMAGE_BAYER_BGGR;
						break;
					case BAYER_ORDER_GBRG:
						brcm_header->mode.bayer_order = VC_IMAGE_BAYER_GBRG;
						break;
					case BAYER_ORDER_GRBG:
						brcm_header->mode.bayer_order = VC_IMAGE_BAYER_GRBG;
						break;
					case BAYER_ORDER_RGGB:
						brcm_header->mode.bayer_order = VC_IMAGE_BAYER_RGGB;
						break;
				}
				switch(cfg.bit_depth)
				{
					case 8:
						brcm_header->mode.bayer_format = VC_IMAGE_BAYER_RAW8;
						break;
					case 10:
						brcm_header->mode.bayer_format = VC_IMAGE_BAYER_RAW10;
						break;
					case 12:
						brcm_header->mode.bayer_format = VC_IMAGE_BAYER_RAW12;
						break;
					case 14:
						brcm_header->mode.bayer_format = VC_IMAGE_BAYER_RAW14;
						break;
					case 16:
						brcm_header->mode.bayer_format = VC_IMAGE_BAYER_RAW16;
						break;
				}
				if (cfg.write_header0)
				{
					// Save bcrm_header into one file only
					FILE *file;
					file = fopen(cfg.write_header0, "wb");
					if (file)
					{
						fwrite(brcm_header, BRCM_RAW_HEADER_LENGTH, 1, file);
						fclose(file);
					}
				}
			}
		}
		else if (cfg.write_headerg)
		{
			// Save pgm_header into one file only
			FILE *file;
			file = fopen(cfg.write_headerg, "wb");
			if (file)
			{
				fprintf(file, "P5\n%d %d\n255\n", sensor_mode->width, sensor_mode->height);
				fclose(file);
			}
		}

		status = mmal_port_parameter_set_boolean(output, MMAL_PARAMETER_ZERO_COPY, MMAL_TRUE);
		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("Failed to set zero copy");
			goto component_disable;
		}

		vcos_log_error("Create pool of %d buffers of size %d", output->buffer_num, output->buffer_size);
		pool = mmal_port_pool_create(output, output->buffer_num, output->buffer_size);
		if (!pool)
		{
			vcos_log_error("Failed to create pool");
			goto component_disable;
		}

		/// By radmonpi++
		pixel_counter = (uint32_t *)malloc(cfg.width*cfg.height*sizeof(uint32_t));
		if (pixel_counter == NULL) {fprintf(stderr, "Couldn't alloc memory"); return -1;}
		sum_value = (uint64_t *)malloc(cfg.width*cfg.height*sizeof(uint64_t));
		if (sum_value == NULL) {fprintf(stderr, "Couldn't alloc memory"); return -1;}
		for(i=0;i<cfg.width*cfg.height;i++) {
			sum_value[i] = 0;
			pixel_counter[i] = 0;
		}
		if (cfg.debug) { fprintf(stderr, "The size of the image is\t%dx%d\n", cfg.height, cfg.width); }
		

		output->userdata = (struct MMAL_PORT_USERDATA_T *)&cfg;
		status = mmal_port_enable(output, callback);

		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("Failed to enable port");
			goto pool_destroy;
		}
		running = 1;

		for(i = 0; i<output->buffer_num; i++)
		{
			MMAL_BUFFER_HEADER_T *buffer = mmal_queue_get(pool->queue);

			if (!buffer)
			{
				vcos_log_error("Where'd my buffer go?!");
				goto port_disable;
			}
			status = mmal_port_send_buffer(output, buffer);
			if (status != MMAL_SUCCESS)
			{
				vcos_log_error("mmal_port_send_buffer failed on buffer %p, status %d", buffer, status);
				goto port_disable;
			}
			vcos_log_error("Sent buffer %p", buffer);
		}
	}

	start_camera_streaming(sensor, sensor_mode);

	if (cfg.timeout==0)
		cfg.timeout= -1;
	vcos_sleep(cfg.timeout*1000);
	running = 0;

	stop_camera_streaming(sensor);

port_disable:
	if (1) // cfg.capture) FAB: Always in capture mode.
	{
		status = mmal_port_disable(output);
		if (status != MMAL_SUCCESS)
		{
			vcos_log_error("Failed to disable port");
			return -1;
		}
	}
pool_destroy:
	if (pool)
		mmal_port_pool_destroy(output, pool);
	if (isp_render)
	{
		mmal_connection_disable(isp_render);
		mmal_connection_destroy(isp_render);
	}
	if (rawcam_isp)
	{
		mmal_connection_disable(rawcam_isp);
		mmal_connection_destroy(rawcam_isp);
	}
component_disable:
	if (brcm_header)
		free(brcm_header);
	status = mmal_component_disable(render);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to disable render");
	}
	status = mmal_component_disable(isp);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to disable isp");
	}
	status = mmal_component_disable(rawcam);
	if (status != MMAL_SUCCESS)
	{
		vcos_log_error("Failed to disable rawcam");
	}
component_destroy:
	if (rawcam)
		mmal_component_destroy(rawcam);
	if (isp)
		mmal_component_destroy(isp);
	if (render)
		mmal_component_destroy(render);

	if (cfg.write_timestamps)
	{
		// Save timestamps
		FILE *file;
		file = fopen(cfg.write_timestamps, "wb");
		if (file)
		{
			int64_t old;
			PTS_NODE_T aux;
			for(aux = cfg.ptsa; aux != cfg.ptso; aux = aux->nxt)
			{
				if (aux == cfg.ptsa)
				{
					fprintf(file, ",%d,%lld\n", aux->idx, aux->pts);
				}
				else
				{
					fprintf(file, "%lld,%d,%lld\n", aux->pts-old, aux->idx, aux->pts);
				}
				old = aux->pts;
			}
			fclose(file);
		}

		while (cfg.ptsa != cfg.ptso)
		{
			PTS_NODE_T aux = cfg.ptsa->nxt;
			free(cfg.ptsa);
			cfg.ptsa = aux;
		}
		free(cfg.ptso);
	}

	uint64_t nBadPixels = 0;
	for (i=0; i<cfg.height*cfg.width; i++)
		if (pixel_counter[i]>=cfg.ntoSave)
			nBadPixels++;

	fprintf(stderr, "Found\t%" PRId64 "\tbad pixels\n",nBadPixels);

	if (cfg.matrix){
		int j;
		printf("P2\n%u\t%u\n%u\n",cfg.width,cfg.height,(1<<cfg.bit_depth)-1);
		for(i=0;i<cfg.height;i++) {
			for(j=0;j<cfg.width-1;j++) {
				printf("%"PRId64"\t",sum_value[j+i*cfg.width]/pixel_counter[j+i*cfg.width]);
			}
			printf("%"PRId64"\n",sum_value[j+i*cfg.width]/pixel_counter[j+i*cfg.width]);
		}
	} else {
		printf("#");
		for (i=0; i<argc;i++) printf("%s ", argv[i]);
		for (i=0; i<cfg.height*cfg.width; i++)
			if (pixel_counter[i]>=cfg.ntoSave)
				printf("\n%u\t%u\t%u\t%"PRId64 ,i%cfg.width, i/cfg.width, pixel_counter[i],sum_value[i]/pixel_counter[i]);
	}

	if (cfg.savepixelhisto)
		if (fprint_histo(cfg.savepixelhisto, pixel_histo)	!= 0)
			fprintf(stderr,"ERROR: Couldn't save charge histogram\n");

	fflush(stdout);

	free(sum_value);
	free(pixel_counter);

	return 0;
}

void modRegBit(struct mode_def *mode, uint16_t reg, int bit, int value, enum operation op)
{
	int i = 0;
	uint16_t val;
	while(i < mode->num_regs && mode->regs[i].reg != reg) i++;
	if (i == mode->num_regs) {
		vcos_log_error("Reg: %04X not found!\n", reg);
		return;
	}
	val = mode->regs[i].data;

	switch(op)
	{
		case EQUAL:
			val = (val | (1 << bit)) & (~( (1 << bit) ^ (value << bit) ));
			break;
		case SET:
			val = val | (1 << bit);
			break;
		case CLEAR:
			val = val & ~(1 << bit);
			break;
		case XOR:
			val = val ^ (value << bit);
			break;
	}
	mode->regs[i].data = val;
}

void modReg(struct mode_def *mode, uint16_t reg, int startBit, int endBit, int value, enum operation op)
{
	int i;
	for(i = startBit; i <= endBit; i++) {
		modRegBit(mode, reg, i, value >> i & 1, op);
	}
}

void update_regs(const struct sensor_def *sensor, struct mode_def *mode, int hflip, int vflip, int exposure, int gain)
{
	if (sensor->vflip_reg)
	{
		modRegBit(mode, sensor->vflip_reg, sensor->vflip_reg_bit, vflip, XOR);
		if (vflip && !sensor->flips_dont_change_bayer_order)
			mode->order ^= 2;
	}

	if (sensor->hflip_reg)
	{
		modRegBit(mode, sensor->hflip_reg, sensor->hflip_reg_bit, hflip, XOR);
		if (hflip && !sensor->flips_dont_change_bayer_order)
			mode->order ^= 1;
	}

	if (sensor->exposure_reg && exposure != -1)
	{
		if (exposure < 0 || exposure >= (1<<sensor->exposure_reg_num_bits))
		{
			vcos_log_error("Invalid exposure:%d, exposure range is 0 to %u!\n",
						exposure, (1<<sensor->exposure_reg_num_bits)-1);
		}
		else
		{
			uint8_t val;
			int i, j=sensor->exposure_reg_num_bits-1;
			int num_regs = (sensor->exposure_reg_num_bits+7)>>3;

			for(i=0; i<num_regs; i++, j-=8)
			{
				val = (exposure >> (j&~7)) & 0xFF;
				modReg(mode, sensor->exposure_reg+i, 0, j&0x7, val, EQUAL);
				vcos_log_error("Set exposure %04X to %02X", sensor->exposure_reg+i, val);
			}
		}
	}
	if (sensor->vts_reg && exposure != -1 && exposure >= mode->min_vts)
	{
		if (exposure < 0 || exposure >= (1<<sensor->vts_reg_num_bits))
		{
			vcos_log_error("Invalid exposure:%d, vts range is 0 to %u!\n",
						exposure, (1<<sensor->vts_reg_num_bits)-1);
		}
		else
		{
			uint8_t val;
			int i, j=sensor->vts_reg_num_bits-1;
			int num_regs = (sensor->vts_reg_num_bits+7)>>3;

			for(i = 0; i<num_regs; i++, j-=8)
			{
				val = (exposure >> (j&~7)) & 0xFF;
				modReg(mode, sensor->vts_reg+i, 0, j&0x7, val, EQUAL);
				vcos_log_error("Set vts %04X to %02X", sensor->vts_reg+i, val);
			}
		}
	}
	if (sensor->gain_reg && gain != -1)
	{
		if (gain < 0 || gain >= (1<<sensor->gain_reg_num_bits))
		{
			vcos_log_error("Invalid gain:%d, gain range is 0 to %u\n",
						gain, (1<<sensor->gain_reg_num_bits)-1);
		}
		else
		{
			uint8_t val;
			int i, j=sensor->gain_reg_num_bits-1;
			int num_regs = (sensor->gain_reg_num_bits+7)>>3;

			for(i = 0; i<num_regs; i++)
			{
				val = (gain >> (j&~7)) & 0xFF;
				modReg(mode, sensor->gain_reg+i, 0, j&0x7, val, EQUAL);
				vcos_log_error("Set gain %04X to %02X", sensor->gain_reg+i, val);
				j -= ((j&0x7)+1);
			}
		}
	}
}

