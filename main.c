#include <stdint.h>
#include <stdio.h>
#include<stdlib.h>
#include <x264.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include <time.h>

#define FAIL_IF_ERROR( cond, ... )\
    do\
{\
    if( cond )\
{\
    fprintf( stderr, __VA_ARGS__ );\
    goto fail;\
    }\
    } while( 0 )

FILE *fp_yuv=NULL,*fp_264=NULL;
char *fileyuv="/home/junlin/opensoure/x264/install/640x480.yuv";
char *file264="hello.264";

int init_x264_param(x264_t **x264_st,int width,int height,x264_picture_t *pic)
{
    x264_param_t param;
    /* Get default params for preset/tuning */
    if(x264_param_default_preset(&param, "medium", NULL ) < 0 )
        return  -1;

    /* Configure non-default params */
    param.i_bitdepth = 8;
    param.i_csp = X264_CSP_I420;
    param.i_width  = width;
    param.i_height = height;
    param.b_vfr_input = 0;
    param.b_repeat_headers = 1;
    param.b_annexb = 1;
    param.i_threads=4;


    /* Apply profile restrictions. */
    if( x264_param_apply_profile(&param, "high" ) < 0 )
        return  -1;

    if( x264_picture_alloc(pic, param.i_csp, param.i_width, param.i_height ) < 0 )
    {
        x264_picture_clean(pic);
        return  -1;
    }
    *x264_st = x264_encoder_open(&param);
    if(!*x264_st ) return -1;

    return 0;

}

int init_yuv_264(FILE **fp_yuv,char *file_yuv,FILE **fp_264,char * file_264)
{
    *fp_yuv = fopen(file_yuv, "rb");
    if(fp_yuv == NULL)
    {
        return -1;
    }
    *fp_264 = fopen(file_264, "wb");
    if(fp_264 == NULL)
    {
        return -1;
    }

    return 0;
}

int write_264_date(x264_t *x264_st ,x264_picture_t *pic_in,\
                   uint8_t *buffer,int width,int height,int i_frame,x264_picture_t *pic_out)
{
    int luma_size = width * height;
    int chroma_size = luma_size / 4;
    x264_nal_t *nal;
    int i_nal;
    /* Read input frame */
    memcpy(pic_in->img.plane[0],buffer, luma_size);
    memcpy(pic_in->img.plane[1],buffer + luma_size, chroma_size);
    memcpy(pic_in->img.plane[2],buffer + luma_size + chroma_size, chroma_size);
    pic_in->i_pts = i_frame;

    int i_frame_size = x264_encoder_encode(x264_st, &nal, &i_nal,pic_in, pic_out );
    if( i_frame_size < 0 )return -1;
    else if( i_frame_size )
    {
        if(!fwrite( nal->p_payload, i_frame_size, 1, fp_264 ))
            return -1;
    }
    printf("i_frame_size=%d %d\r\n",i_frame_size,luma_size);
    return i_frame_size;
}

int flush_264_date(x264_t *x264_st,x264_picture_t *pic_out)
{
    int i_frame_size=0;
    x264_nal_t *nal;
    int i_nal;
    while(x264_encoder_delayed_frames(x264_st) )
    {
        i_frame_size = x264_encoder_encode(x264_st, &nal, &i_nal, NULL, pic_out );
        if( i_frame_size < 0 )
            return  -1;
        else if( i_frame_size )
        {
            if( !fwrite( nal->p_payload, i_frame_size, 1, fp_264 ) )
                return 0;
        }
    }
}
/**
操作步骤：
1. 打开设备文件。 int fd=open(”/dev/video0″,O_RDWR);
2. 取得设备的capability，看看设备具有什么功能，比如是否具有视频输入,或者音频输入输出等。VIDIOC_QUERYCAP,struct v4l2_capability
3. 选择视频输入，一个视频设备可以有多个视频输入。VIDIOC_S_INPUT,struct v4l2_input
4. 设置视频的制式和帧格式，制式包括PAL，NTSC，帧的格式个包括宽度和高度等。
VIDIOC_S_STD,VIDIOC_S_FMT,struct v4l2_std_id,struct v4l2_format
5. 向驱动申请帧缓冲，一般不超过5个。struct v4l2_requestbuffers
6. 将申请到的帧缓冲映射到用户空间，这样就可以直接操作采集到的帧了，而不必去复制。mmap
7. 将申请到的帧缓冲全部入队列，以便存放采集到的数据.VIDIOC_QBUF,struct v4l2_buffer
8. 开始视频的采集。VIDIOC_STREAMON
9. 出队列以取得已采集数据的帧缓冲，取得原始采集数据。VIDIOC_DQBUF
10. 将缓冲重新入队列尾,这样可以循环采集。VIDIOC_QBUF
11. 停止视频的采集。VIDIOC_STREAMOFF
12. 关闭视频设备。close(fd);
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>


#pragma pack(1)
typedef struct BITMAPFILEHEADER
{
    unsigned short bfType;//位图文件的类型,
    unsigned long bfSize;//位图文件的大小，以字节为单位
    unsigned short bfReserved1;//位图文件保留字，必须为0
    unsigned short bfReserved2;//同上
    unsigned long bfOffBits;//位图阵列的起始位置，以相对于位图文件   或者说是头的偏移量表示，以字节为单位
} BITMAPFILEHEADER;
#pragma pack()
typedef struct BITMAPINFOHEADER//位图信息头类型的数据结构，用于说明位图的尺寸
{
    unsigned long biSize;//位图信息头的长度，以字节为单位
    unsigned long biWidth;//位图的宽度，以像素为单位
    unsigned long biHeight;//位图的高度，以像素为单位
    unsigned short biPlanes;//目标设备的级别,必须为1
    unsigned short biBitCount;//每个像素所需的位数，必须是1(单色),4(16色),8(256色)或24(2^24色)之一
    unsigned long biCompression;//位图的压缩类型，必须是0-不压缩，1-BI_RLE8压缩类型或2-BI_RLE4压缩类型之一
    unsigned long biSizeImage;//位图大小，以字节为单位
    unsigned long biXPelsPerMeter;//位图目标设备水平分辨率，以每米像素数为单位
    unsigned long biYPelsPerMeter;//位图目标设备垂直分辨率，以每米像素数为单位
    unsigned long biClrUsed;//位图实际使用的颜色表中的颜色变址数
    unsigned long biClrImportant;//位图显示过程中被认为重要颜色的变址数
} BITMAPINFOHEADER;

void yuv422_2_rgb();
#define videocount  3
#define JEPG_FILE 	"yuyv.jpg"
#define RGB_FILE	"rgb.bmp"
#define YUV_VIDEO	"video.yuv"

static int fd = 0;
static int width = 640;
static int height = 480;
static struct v4l2_fmtdesc fmtdesc;
struct videobuffer{
    unsigned int length;
    void* start;
};

static struct videobuffer framebuf[videocount];
static struct v4l2_buffer buf;

unsigned char* starter;
unsigned char* newBuf;
struct BITMAPFILEHEADER bfh;
struct BITMAPINFOHEADER bih;

x264_picture_t pic_in;
x264_picture_t pic_out;
x264_t *x264_st=NULL;
FILE *file;
int yuyv_to_yuv420p(const unsigned char *in, unsigned char *out, unsigned int width, unsigned int height)
{
    unsigned char *y = out;
    unsigned char *u = out + width*height;
    unsigned char *v = out + width*height + width*height/4;

    unsigned int i,j;
    unsigned int base_h;
    unsigned int is_u = 1;
    unsigned int y_index = 0, u_index = 0, v_index = 0;

    unsigned long yuv422_length = 2 * width * height;

    //序列为YU YV YU YV，一个yuv422帧的长度 width * height * 2 个字节
    //丢弃偶数行 u v
    for(i=0; i<yuv422_length; i+=2)
    {
        *(y+y_index) = *(in+i);
        y_index++;
    }
    for(i=0; i<height; i+=2)
    {
        base_h = i*width*2;
        for(j=base_h+1; j<base_h+width*2; j+=2)
        {
            if(is_u)
            {
                *(u+u_index) = *(in+j);
                u_index++;
                is_u = 0;
            }
            else
            {
                *(v+v_index) = *(in+j);
                v_index++;
                is_u = 1;
            }
        }
    }
    return 1;
}
void create_bmp_header()
{
    bfh.bfType = (unsigned short)0x4D42;
    bfh.bfSize = (unsigned long)(14 + 40 + width * height*3);
    bfh.bfReserved1 = 0;
    bfh.bfReserved2 = 0;
    bfh.bfOffBits= (unsigned long)(14 + 40);

    bih.biBitCount = 24;
    bih.biWidth = width;
    bih.biHeight = height;
    bih.biSizeImage = width * height * 3;
    bih.biClrImportant = 0;
    bih.biClrUsed = 0;
    bih.biCompression = 0;
    bih.biPlanes = 1;
    bih.biSize = 40;//sizeof(bih);
    bih.biXPelsPerMeter = 0x00000ec4;
    bih.biYPelsPerMeter=0x00000ec4;
}


/* 1\打开设备 */
int openCamera(int id)
{
    char devicename[12];;
    sprintf(devicename,"/dev/video%d",id);
    fd = open(devicename, O_RDWR | O_NONBLOCK, 0);
    if(fd <0 ){
        printf("open video0 fail.\n");
        return -1;
    }
    return 0;
}
/* 2、查看设备能力 */
void capabilityCamera()
{
    struct v4l2_capability cap;
    ioctl(fd, VIDIOC_QUERYCAP, &cap);
    printf("--------------capability------------------\n");
    printf("driver:%s    \ncard:%s   \ncapabilities:%x\n",cap.driver,cap.card,cap.capabilities);
}

/* 3、查看支持的数据格式 */
void enumfmtCamera()
{
    int ret;
    int i;
    memset(&fmtdesc, 0, sizeof(fmtdesc));
    fmtdesc.index = 0;
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    printf("-------------VIDIOC_ENUM_FMT--------------\n");
    while((ioctl(fd, VIDIOC_ENUM_FMT, &fmtdesc)) != -1)
    {
        printf("index:%d   \npixelformat:%c%c%c%c  \ndescription:%s\n",fmtdesc.index, fmtdesc.pixelformat&0xff,(fmtdesc.pixelformat>>8)&0xff,(fmtdesc.pixelformat>>16)&0xff,
               (fmtdesc.pixelformat>>24)&0xff,fmtdesc.description);
        fmtdesc.index++;
    }
}

/* 4、设置视频格式 VIDIOC_S_FMT struct v4l2_format */
int setfmtCamera()
{
    int ret;
    struct v4l2_format format;
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    format.fmt.pix.width = width;
    format.fmt.pix.height = height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;  // 设置为yuyv格式数据
    format.fmt.pix.field = V4L2_FIELD_INTERLACED;
    ret = ioctl(fd, VIDIOC_S_FMT, &format);
    if(ret < 0){
        printf("VIDIOC_S_FMT fail\n");
        return -1;
    }
    memset(&format, 0, sizeof(format));
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_G_FMT, &format);
    if(ret < 0)
    {
        printf("VIDIOC_G_FMT fail\n");
        return -1;
    }
    printf("-----------------VIDIOC_G_FMT----------------------\n");
    printf("width:%d   \nheight:%d   \ntype:%x   pixelformat:%c%c%c%c\n",format.fmt.pix.width,format.fmt.pix.height,
           format.type,format.fmt.pix.pixelformat&0xff,(format.fmt.pix.pixelformat>>8)&0xff,(format.fmt.pix.pixelformat>>16)&0xff,
           (format.fmt.pix.pixelformat>>24)&0xff);
    return 0;
}

/* 5、申请内存作为缓冲区VIDIOC_REQBUFS struct v4l2_requestbuffers，
*  查询缓冲区状态后映射到用于空间 VIDIOC_QUERYBUF struct v4l2_buffer mmap，然后将缓冲区放入队列 VIDIOC_QBUF
*/
int initmmap()
{
    struct v4l2_requestbuffers reqbuf;
    int i, ret;
    reqbuf.count = videocount;
    reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf.memory = V4L2_MEMORY_MMAP;
    ret = ioctl(fd, VIDIOC_REQBUFS, &reqbuf);
    if(0 != ret){
        printf("VIDIOC_REQBUFS fail\n");
        return -1;
    }
    //v4l2_buffer
    printf("----------------mmap----------------\n");
    for(i =0; i < reqbuf.count; i++){
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.index = i;
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        ret = ioctl(fd, VIDIOC_QUERYBUF, &buf);

        framebuf[i].length = buf.length;
        framebuf[i].start = mmap(NULL, buf.length, PROT_READ|PROT_WRITE,
                                 MAP_SHARED, fd, buf.m.offset);
        if(framebuf[i].start == MAP_FAILED){
            perror("mmap fail.\n");
            return -1;
        }
        printf("start:%x  length:%d\n",(unsigned int)framebuf[i].start,framebuf[i].length);
    }
    return 0;
}


/* 6、开始采集视频数据
 * 将缓冲区如队列 VIDIOC_QBUF  struct v4l2_buffer
 * 开始流传输 VIDIOC_STREAMON
 */
static int startcap()
{
    int ret = -1, i = 0;

    for(i=0;i < videocount; i++){
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        ret = ioctl(fd, VIDIOC_QBUF, &buf);
        if(0 != ret){
            perror("VIDIOC_QBUF fail.\n");
            return -1;
        }
    }

    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd, VIDIOC_STREAMON, &type);
    return 0;
}
/* 6、判断缓冲区是否有数据 使用poll函数
 *  缓冲区有数据后取出队列 VIDIOC_DQBUF
 */

static int readfram()
{
    struct pollfd pollfd;
    int ret;
    unsigned char * yuv_420p=(unsigned char *)malloc(width*height*3);
    for(int llp=0;llp<1000;llp++){
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm tm;
        localtime_r(&tv.tv_sec, &tm);
        printf("[%02d:%02d:%02d:%03ld]llp=%d\r\n",
               tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec / 1000,llp);

        memset(&pollfd, 0, sizeof(pollfd));
        pollfd.fd = fd;
        pollfd.events = POLLIN;
        ret = poll(&pollfd, 1, 800);
        if(-1 == ret){
            perror("VIDIOC_QBUF fail.\n");
            return -1;
        }else if(0 == ret){
            printf("poll time out\n");
            continue;
        }
        //printf("-------------poll success---------------\n");
        // static struct v4l2_buffer buf;

        if(pollfd.revents & POLLIN){
            memset(&buf, 0, sizeof(buf));
            buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            buf.memory = V4L2_MEMORY_MMAP;
            ret = ioctl(fd, VIDIOC_DQBUF, &buf);
            if(0 != ret){
                perror("VIDIOC_QBUF fail.\n");
                return -1;
            }
            // 直接保存的yuyv数据的视频
            yuyv_to_yuv420p((unsigned char*)framebuf[buf.index].start,yuv_420p,width,height);
            //ret = fwrite(yuv_420p, 1, width*height+(width*height/2), file);
            write_264_date(x264_st,&pic_in,yuv_420p,width,height,llp,&pic_out);

            // RGB格式数据的bmp图片
            /*
        starter = (unsigned char*)framebuf[buf.index].start;
        newBuf = (unsigned char*)calloc((unsigned int)(framebuf[buf.index].length*3/2),sizeof(unsigned char));
        yuv422_2_rgb();
        create_bmp_header();

        //yuyv422toBGRY(starter);
        sprintf(filename,"rgb%d.bmp",i);
        FILE *file1 = fopen(filename, "wb");
        fwrite(&bfh,sizeof(bfh),1,file1);
        fwrite(&bih,sizeof(bih),1,file1);
        fwrite(newBuf, 1, buf.length*3/2, file1);
        //fwrite(rgb, 1, width*height*3, file1);
        fclose(file1);
    */

            ret = ioctl(fd, VIDIOC_QBUF, &buf);
        }
    }
    free(yuv_420p);
    flush_264_date(x264_st,&pic_out);
    return ret;
}



/* 最后关闭摄像头数据流和设备 */
static void closeCamera()
{
    int ret=-1, i;
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ret = ioctl(fd,VIDIOC_STREAMOFF, &type);
    if(0 != ret){
        perror("VIDIOC_QBUF fail.\n");
        return ;
    }
    for(i = 0; i < videocount; i++){
        munmap(framebuf[i].start, framebuf[i].length);
    }
    close(fd);
}


void yuv422_2_rgb()
{
    unsigned char YUV[4],RGB[6];
    int i,j,k=0;
    unsigned int location = 0;
    for(i = 0;i < framebuf[buf.index].length; i+=4)
    {
        YUV[0] = starter[i];		// y
        YUV[1] = starter[i+1];		// u
        YUV[2] = starter[i+2];		// y
        YUV[3] = starter[i+3];		// v
        if(YUV[0] < 0){
            RGB[0]=0;
            RGB[1]=0;
            RGB[2]=0;
        }else{
            RGB[0] = YUV[0] + 1.772*(YUV[1]-128);		// b
            RGB[1] = YUV[0] - 0.34414*(YUV[1]-128) - 0.71414*(YUV[3]-128);		// g
            RGB[2] = YUV[0 ]+ 1.402*(YUV[3]-128);			// r
        }
        if(YUV[2] < 0)
        {
            RGB[3]=0;
            RGB[4]=0;
            RGB[5]=0;
        }else{
            RGB[3] = YUV[2] + 1.772*(YUV[1]-128);		// b
            RGB[4] = YUV[2] - 0.34414*(YUV[1]-128) - 0.71414*(YUV[3]-128);		// g
            RGB[5] = YUV[2] + 1.402*(YUV[3]-128) ;			// r
        }

        for(j = 0; j < 6; j++){
            if(RGB[j] < 0)
                RGB[j] = 0;
            if(RGB[j] > 255)
                RGB[j] = 255;
        }
        //请记住：扫描行在位图文件中是反向存储的！
        if(k%(width*3)==0)//定位存储位置
        {
            location=(height-k/(width*3))*(width*3);
        }
        bcopy(RGB,newBuf+location+(k%(width*3)),sizeof(RGB));
        k+=6;
    }
    return ;
}

int main(int argc, char* argv[])
{
    init_x264_param(&x264_st,width,height,&pic_in);
    init_yuv_264(&fp_yuv,fileyuv,&fp_264,file264);
    openCamera(0);
    capabilityCamera();
    //enumfmtCamera();
    setfmtCamera();
    initmmap();
    startcap();
    file=fopen(YUV_VIDEO,"w+");
    readfram();
    closeCamera();
    x264_encoder_close(x264_st);
    x264_picture_clean( &pic_in );
    fclose(fp_264);
    fclose(file);
    return 0;

}
