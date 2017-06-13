/*
* 语音语义技术能够将语音听写业务中的内容进行语义解析。
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/soundcard.h>
#include "qtts.h"
#include "qisr.h"
#include "msp_cmn.h"
#include "msp_errors.h"
#include "speech_recognizer.h"

#include "cJSON.h"

#define Audio_Device "/dev/dsp"//播放设备

#define	BUFFER_SIZE	4096
#define FRAME_LEN	640 
#define HINTS_SIZE  100

//不同的声卡有着不同的播放参数，这些参数可以使用file命令获得

#define Sample_Size 16 //there're two kinds of bits,8 bits and 16 bits

#define Sample_Rate 16000 //sampling rate

/*
	* sub:				请求业务类型
	* domain:			领域
	* language:			语言
	* accent:			方言
	* sample_rate:		音频采样率
	* result_type:		识别结果格式
	* result_encoding:	结果编码格式
	*
	* nlp_version:      语义版本
	* sch:              是否使用语义标识
	* 详细参数说明请参阅《iFlytek MSC Reference Manual》
	*/
const char* session_begin_params =
		"sub = iat, domain = iat, language = zh_cn, "
		"accent = mandarin, sample_rate = 16000, "
		"result_type = plain, result_encoding = utf-8,vad_bos = 30000,vad_eos = 2000";

const char* session_begin_params_tts = "voice_name = yefang, text_encoding = utf8, sample_rate = 16000, speed = 50, volume = 100, pitch = 50, rdn = 2";

const char* session_begin_params_sch ="nlp_version =2.0,sch=1,sub=iat,domain = iat, language = zh_cn, accent = mandarin,aue = ico, sample_rate = 16000, result_type = plain, result_encoding = utf-8,vad_bos = 30000,vad_eos = 2000";
	
const char* sr_filename  = "schh.wav"; //合成的语音文件名称,输入语音
const char* rp_filename  = "repeat.wav"; //合成的语音文件名称,请求再说一次
const char* by_filename  = "beyond.wav";//合成的语音文件名称,失败语音
const char *file_name = "result.wav";//返回的结果文件名称
const char* text = "亲，有什么需要小派服务的吗";//开始交互
const char* repeat_text="刚才小派出去玩了，你说了有趣的事吗";//未检测到说话
const char* beyond_text="你说的可难倒小派了呀";//无结果
int repeat=0;

typedef struct _wave_pcm_hdr
  {
         char            riff[4];                // = "RIFF"
         int             size_8;                 // = FileSize - 8
         char            wave[4];                // = "WAVE"
         char            fmt[4];                 // = "fmt "
         int             fmt_size;               // = 下一个结构体的大小 : 16
 
         short int       format_tag;             // = PCM : 1
         short int       channels;               // = 通道数 : 1
         int             samples_per_sec;        // = 采样率 : 8000 | 6000 | 11025 | 16000           
         int             avg_bytes_per_sec;      // = 每秒字节数 : samples_pe r_sec * bits_per_sample / 8
         short int       block_align;            // = 每采样点字节数 : wBitsP erSample / 8
         short int       bits_per_sample;        // = 量化比特数: 8 | 16

         char            data[4];                // = "data";
         int             data_size;              // = 纯数据长度 : FileSize -  44 
 } wave_pcm_hdr;


/* 默认wav音频头部数据 */
wave_pcm_hdr default_wav_hdr = 
{
	{ 'R', 'I', 'F', 'F' },
	0,
	{'W', 'A', 'V', 'E'},
	{'f', 'm', 't', ' '},
	16,
	1,
	1,
	16000,
	32000,
	2,
	16,
	{'d', 'a', 't', 'a'},
	0  
};
/* 文本合成 */
int text_to_speech(const char* src_text, const char* des_path, const char* params)
{
	int          ret          = -1;
	FILE*        fp           = NULL;
	const char*  sessionID    = NULL;
	unsigned int audio_len    = 0;
	wave_pcm_hdr wav_hdr      = default_wav_hdr;
	int          synth_status = MSP_TTS_FLAG_STILL_HAVE_DATA;

	if (NULL == src_text || NULL == des_path)
	{
		printf("params is error!\n");
		return ret;
	}
	fp = fopen(des_path, "wb");
	if (NULL == fp)
	{
		printf("open %s error.\n", des_path);
		return ret;
	}
	/* 开始合成 */
	sessionID = QTTSSessionBegin(params, &ret);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionBegin failed, error code: %d.\n", ret);
		fclose(fp);
		return ret;
	}
	ret = QTTSTextPut(sessionID, src_text, (unsigned int)strlen(src_text), NULL);
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSTextPut failed, error code: %d.\n",ret);
		QTTSSessionEnd(sessionID, "TextPutError");
		fclose(fp);
		return ret;
	}
	printf("正在合成 ...\n");
	fwrite(&wav_hdr, sizeof(wav_hdr) ,1, fp); //添加wav音频头，使用采样率为16000
	while (1) 
	{
		/* 获取合成音频 */
		const void* data = QTTSAudioGet(sessionID, &audio_len, &synth_status, &ret);
		if (MSP_SUCCESS != ret)
			break;
		if (NULL != data)
		{
			fwrite(data, audio_len, 1, fp);
		    wav_hdr.data_size += audio_len; //计算data_size大小
		}
		if (MSP_TTS_FLAG_DATA_END == synth_status)
			break;
		printf(">");
		usleep(150*1000); //防止频繁占用CPU
	}//合成状态synth_status取值请参阅《讯飞语音云API文档》
	printf("\n");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSAudioGet failed, error code: %d.\n",ret);
		QTTSSessionEnd(sessionID, "AudioGetError");
		fclose(fp);
		return ret;
	}
	/* 修正wav文件头数据的大小 */
	wav_hdr.size_8 += wav_hdr.data_size + (sizeof(wav_hdr) - 8);
	
	/* 将修正过的数据写回文件头部,音频文件为wav格式 */
	fseek(fp, 4, 0);
	fwrite(&wav_hdr.size_8,sizeof(wav_hdr.size_8), 1, fp); //写入size_8的值
	fseek(fp, 40, 0); //将文件指针偏移到存储data_size值的位置
	fwrite(&wav_hdr.data_size,sizeof(wav_hdr.data_size), 1, fp); //写入data_size的值
	fclose(fp);
	fp = NULL;
	/* 合成完毕 */
	ret = QTTSSessionEnd(sessionID, "Normal");
	if (MSP_SUCCESS != ret)
	{
		printf("QTTSSessionEnd failed, error code: %d.\n",ret);
	}

	return ret;
}
/*显示结果*/
static void show_result(char *string, char is_over)
{
	printf("\rResult: [ %s ]", string);

    repeat=1;

	if(is_over)
	{
		putchar('\n');

		//合成识别结果
		printf("开始合成 ...\n");
	
    	int ret = text_to_speech(string, sr_filename, session_begin_params_tts);
   		 if (MSP_SUCCESS != ret)
    	 {
        	 printf("text_to_speech failed, error code: %d.\n", ret);
     	}
     	printf("合成完毕\n");

	    play_sound("schh.wav"); 
     //	system("play schh.wav");

   //     usleep(50*1000); //防止频繁占用CPU 
  
 //    	run_iat("schh.wav", session_begin_params_sch, session_begin_params_tts);


    }
}

static char *g_result = NULL;
static unsigned int g_buffersize = BUFFER_SIZE;

void on_result(const char *result, char is_last)
{
	if (result) {
		size_t left = g_buffersize - 1 - strlen(g_result);
		size_t size = strlen(result);
		if (left < size) {
			g_result = (char*)realloc(g_result, g_buffersize + BUFFER_SIZE);
			if (g_result)
				g_buffersize += BUFFER_SIZE;
			else {
				printf("mem alloc failed\n");
				return;
			}
		}
		strncat(g_result, result, size);
		show_result(g_result, is_last);
	}
}
void on_speech_begin()
{
	if (g_result)
	{
		free(g_result);
	}
	g_result = (char*)malloc(BUFFER_SIZE);
	g_buffersize = BUFFER_SIZE;
	memset(g_result, 0, g_buffersize);

	printf("Start Listening...\n");
    
    repeat=0;
}
void on_speech_end(int reason)
{
	if (reason == END_REASON_VAD_DETECT)
		printf("\nSpeaking done \n");
	else
		printf("\nRecognizer error %d\n", reason);
}

/* demo recognize the audio from microphone */
static void demo_mic(const char* session_begin_params)
{
	int errcode;
	int i = 0;

	struct speech_rec iat;

	struct speech_rec_notifier recnotifier = {
		on_result,
		on_speech_begin,
		on_speech_end
	};

	errcode = sr_init(&iat, session_begin_params, SR_MIC, &recnotifier);
	if (errcode) {
		printf("speech recognizer init failed\n");
		return;
	}
	errcode = sr_start_listening(&iat);
	if (errcode) {
		printf("start listen failed %d\n", errcode);
	}
	/* 5 seconds recording */
	while(i++ < 8)
		sleep(1);
	errcode = sr_stop_listening(&iat);
	if (errcode) {
		printf("stop listening failed %d\n", errcode);
	}

	sr_uninit(&iat);
}

int play_sound(char *filename)

{

    struct stat stat_buf;

    unsigned char *buf = NULL;

    int handler,fd;

    int result;

    int arg,status;

  

    //打开声音文件，将文件读入内存

    fd=open(filename,O_RDONLY);

    if(fd<0) 

     return -1;

    if(fstat(fd,&stat_buf))

    {

        close(fd);

        return -1;

    }
    if(!stat_buf.st_size)

    {

        close(fd);

        return -1;

   }

   buf=malloc(stat_buf.st_size);

   if(!buf)

   {

       close(fd);

       return -1;

   }

   if(read(fd,buf,stat_buf.st_size)<0)

   {

       free(buf);

       close(fd);

       return -1;

   }

   //打开声卡设备，并设置声卡播放参数，这些参数必须与声音文件参数一致

   handler=open(Audio_Device,O_WRONLY);

   if(handler==-1)

   {

       perror("open Audio_Device fail");

       return -1;

   }

 

   arg=Sample_Rate;

   status=ioctl(handler,SOUND_PCM_WRITE_RATE,&arg);

   if(status==-1)

   {

       perror("error from SOUND_PCM_WRITE_RATE ioctl");

       return -1;

   }

   arg=Sample_Size;

   status=ioctl(handler,SOUND_PCM_WRITE_BITS,&arg);

   if(status==-1)

   {

       perror("error from SOUND_PCM_WRITE_BITS ioctl");

       return -1;

   }

  //向端口写入，播放

   result=write(handler,buf,stat_buf.st_size);

   if(result==-1)

   {

       perror("Fail to play the sound!");

       return -1;

   }

  

   free(buf);

   close(fd);

   close(handler);

  

    return result;

}

void run_iat(const char* audio_file, const char* session_begin_params,const char* session_begin_params_tts)
{
	const char*		session_id					=	NULL;
	char			rec_result[BUFFER_SIZE]		=	{NULL};	
	char			hints[HINTS_SIZE]			=	{NULL}; //hints为结束本次会话的原因描述，由用户自定义
	unsigned int	total_len					=	0; 
	int				aud_stat					=	MSP_AUDIO_SAMPLE_CONTINUE ;		//音频状态
	int				ep_stat						=	MSP_EP_LOOKING_FOR_SPEECH;		//端点检测
	int				rec_stat					=	MSP_REC_STATUS_SUCCESS ;		//识别状态
	int				errcode						=	MSP_SUCCESS ;

	FILE*			f_pcm						=	NULL;
	char*			p_pcm						=	NULL;
	long			pcm_count					=	0;
	long			pcm_size					=	0;
	long			read_size					=	0;

	
	if (NULL == audio_file)
		goto iat_exit;

	f_pcm = fopen(audio_file, "rb");
	if (NULL == f_pcm) 
	{
		printf("\nopen [%s] failed! \n", audio_file);
		goto iat_exit;
	}
	
	fseek(f_pcm, 0, SEEK_END);
	pcm_size = ftell(f_pcm); //获取音频文件大小 
	fseek(f_pcm, 0, SEEK_SET);		

	p_pcm = (char *)malloc(pcm_size);
    if (NULL == p_pcm)
	{
		printf("\nout of memory! \n");
		goto iat_exit;
	}

	read_size = fread((void *)p_pcm, 1, pcm_size, f_pcm); //读取音频文件内容
	if (read_size != pcm_size)
	{
		printf("\nread [%s] error!\n", audio_file);
		goto iat_exit;
	}
	
	session_id = QISRSessionBegin(NULL, session_begin_params, &errcode); //听写不需要语法，第一个参数为NULL
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRSessionBegin failed! error code:%d\n", errcode);
		goto iat_exit;
	}
	
	while (1) 
	{
		unsigned int len = 10 * FRAME_LEN; // 每次写入200ms音频(16k，16bit)：1帧音频20ms，10帧=200ms。16k采样率的16位音频，一帧的大小为640Byte
		int ret = 0;

		if (pcm_size < 2 * len) 
			len = pcm_size;
		if (len <= 0)
			break;

		aud_stat = MSP_AUDIO_SAMPLE_CONTINUE;
		if (0 == pcm_count)
			aud_stat = MSP_AUDIO_SAMPLE_FIRST;

		printf(">");
		ret = QISRAudioWrite(session_id, (const void *)&p_pcm[pcm_count], len, aud_stat, &ep_stat, &rec_stat);
		if (MSP_SUCCESS != ret)
		{
			printf("\nQISRAudioWrite failed! error code:%d\n", ret);
			goto iat_exit;
		}
			
		pcm_count += (long)len;
		pcm_size  -= (long)len;
		
		if (MSP_REC_STATUS_SUCCESS == rec_stat) //已经有部分听写结果
		{
			const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
			if (MSP_SUCCESS != errcode)
			{
				printf("\nQISRGetResult failed! error code: %d\n", errcode);
				goto iat_exit;
			}
			if (NULL != rslt)
			{
				unsigned int rslt_len = strlen(rslt);
				total_len += rslt_len;
				if (total_len >= BUFFER_SIZE)
				{
					printf("\nno enough buffer for rec_result !\n");
					goto iat_exit;
				}
				strncat(rec_result, rslt, rslt_len);
			}
		}

		if (MSP_EP_AFTER_SPEECH == ep_stat)
			break;
		usleep(200*1000); //模拟人说话时间间隙。200ms对应10帧的音频
	}
	errcode = QISRAudioWrite(session_id, NULL, 0, MSP_AUDIO_SAMPLE_LAST, &ep_stat, &rec_stat);
	if (MSP_SUCCESS != errcode)
	{
		printf("\nQISRAudioWrite failed! error code:%d \n", errcode);
		goto iat_exit;	
	}

	while (MSP_REC_STATUS_COMPLETE != rec_stat) 
	{
		const char *rslt = QISRGetResult(session_id, &rec_stat, 0, &errcode);
		if (MSP_SUCCESS != errcode)
		{
			printf("\nQISRGetResult failed, error code: %d\n", errcode);
			goto iat_exit;
		}
		if (NULL != rslt)
		{
			unsigned int rslt_len = strlen(rslt);
			total_len += rslt_len;
			if (total_len >= BUFFER_SIZE)
			{
				printf("\nno enough buffer for rec_result !\n");
				goto iat_exit;
			}
			strncat(rec_result, rslt, rslt_len);
		}
		usleep(150*1000); //防止频繁占用CPU
	}
	printf("\n语音语义解析结束\n");
   	printf("=============================================================\n");
	printf("%s\n\n",rec_result);

	if(strlen(rec_result)==0)
	{
		printf("\n结果为空\n");
		goto iat_exit;
	}

	cJSON *root = cJSON_Parse(rec_result);  

	int rc = cJSON_GetObjectItem(root,"rc")->valueint;
    	if(rc!=0)
	{
		printf("\n无结果返回!\n");
		play_sound("beyond.wav");
		//system("play beyond.wav"); 
		goto iat_exit;
	}

	char *service = cJSON_GetObjectItem(root,"service")->valuestring; //服务类别判断

  	if (strcmp(service, "weather") == 0)
	{
		cJSON *data = cJSON_GetObjectItem(root,"data");
	    cJSON *result =cJSON_GetObjectItem(data,"result");
	    cJSON *res = cJSON_GetArrayItem(result,0);
		char *sourceName = cJSON_GetObjectItem(res,"sourceName")->valuestring;
		char *date = cJSON_GetObjectItem(res,"date")->valuestring; 
		char *city = cJSON_GetObjectItem(res,"city")->valuestring; 
		char *weather = cJSON_GetObjectItem(res,"weather")->valuestring;
		char *tempRange = cJSON_GetObjectItem(res,"tempRange")->valuestring; 
		char *wind = cJSON_GetObjectItem(res,"wind")->valuestring; 
		char _answer_[BUFFER_SIZE];
		  
		sprintf(_answer_,"来自%s,当地时间%s,地点%s的天气是%s%s %s\n",sourceName,date,city,weather,tempRange,wind);
		printf("%s\n",_answer_);

		printf("开始合成 ...\n");
	    
	   	int ret = text_to_speech(_answer_, file_name, session_begin_params_tts);
	    if (MSP_SUCCESS != ret)
	    {
	         printf("text_to_speech failed, error code: %d.\n", ret);
	   	}
	   	printf("合成完毕\n");

	    play_sound("result.wav");
	    //system("play result.wav"); 

		printf("=============================================================\n");
	}

	else if(strcmp(service, "music") == 0)
	{
		cJSON *data = cJSON_GetObjectItem(root,"data");
	    cJSON *result =cJSON_GetObjectItem(data,"result");
	    cJSON *res = cJSON_GetArrayItem(result,0);
		char *singer = cJSON_GetObjectItem(res,"singer")->valuestring;
		char *name = cJSON_GetObjectItem(res,"name")->valuestring; 
		char *downloadUrl = cJSON_GetObjectItem(res,"downloadUrl")->valuestring; 
		char _answer_[BUFFER_SIZE];
		  
		sprintf(_answer_,"已为您找到由歌手%s演唱的%s\n",singer,name);
		printf("%s\n",_answer_);

		printf("开始合成 ...\n");
	    
	   	int ret = text_to_speech(_answer_, file_name, session_begin_params_tts);
	    if (MSP_SUCCESS != ret)
	    {
	         printf("text_to_speech failed, error code: %d.\n", ret);
	   	}
	   	printf("合成完毕\n");

	    play_sound("result.wav");
	    //system("play result.wav"); 

	    sprintf(_answer_,"wget -O music_download.mp3 %s",downloadUrl);//准备播放音乐
	    system(_answer_);
	
		play_sound("music_download.mp3");

	    //system("play music_download.mp3");

	    system("rm music_download.mp3");

		printf("=============================================================\n");
	}

	else if (strcmp(service, "light_smartHome") == 0)
	{
		cJSON *semantic = cJSON_GetObjectItem(root,"semantic");
	    cJSON *slots =cJSON_GetObjectItem(semantic,"slots");
	    
		char *attrValue = cJSON_GetObjectItem(slots,"attrValue")->valuestring;
		char *attr = cJSON_GetObjectItem(slots,"attr")->valuestring; 
		char _answer_[BUFFER_SIZE];

		int operation = 0;

		if ((strcmp(attrValue, "开") == 0) && (strcmp(attr, "开关") == 0))
		{
			operation = 1;
		}
		else //如果增加亮度属性需修改
			operation = 0;
		  
		sprintf(_answer_,"正在为您%s电灯\n",attrValue);
		printf("%s\n",_answer_);

		printf("开始合成 ...\n");
	    
	   	int ret = text_to_speech(_answer_, file_name, session_begin_params_tts);
	    if (MSP_SUCCESS != ret)
	    {
	         printf("text_to_speech failed, error code: %d.\n", ret);
	   	}
	   	printf("合成完毕\n");

	    play_sound("result.wav");
	    //system("play result.wav"); 

		printf("=============================================================\n");
	}
	else if(strcmp(service, "openQA") == 0||strcmp(service, "calc") == 0||strcmp(service, "datetime") == 0
			||strcmp(service, "baike") == 0||strcmp(service, "chat") == 0||strcmp(service, "faq") == 0)
	{
		cJSON *answer = cJSON_GetObjectItem(root,"answer"); 

	 	char *answer_text = cJSON_GetObjectItem(answer,"text")->valuestring;                       
	 	char _answer_[BUFFER_SIZE];    

	 	sprintf(_answer_,"%s",answer_text);                           
	   	printf("%s\n",_answer_); 

	   	printf("开始合成 ...\n");
	   
	   	int ret = text_to_speech(_answer_, file_name, session_begin_params_tts);
	    if (MSP_SUCCESS != ret)
	    {
	         printf("text_to_speech failed, error code: %d.\n", ret);
	   	}
	   	printf("合成完毕\n");

	    play_sound("result.wav");
	    //system("play result.wav"); 

		printf("=============================================================\n");
	}
	else
	{
		printf("\n不在服务范围\n");
		play_sound("beyond.wav");		
		//system("play beyond.wav"); 
	}
    

iat_exit:
	if (NULL != f_pcm)
	{
		fclose(f_pcm);
		f_pcm = NULL;
	}
	if (NULL != p_pcm)
	{	free(p_pcm);
		p_pcm = NULL;
	}

	QISRSessionEnd(session_id, hints);
}

int main(int argc, char* argv[])
{
	int			ret						=	MSP_SUCCESS;
	int			upload_on				=	1; //是否上传用户词表
	const char* login_params			=	"appid = 57c2c563"; // 登录参数，appid与msc库绑定,请勿随意改动

	/* 用户登录 */
	ret = MSPLogin(NULL, NULL, login_params); //第一个参数是用户名，第二个参数是密码，均传NULL即可，第三个参数是登录参数	
	if (MSP_SUCCESS != ret)
	{
		printf("MSPLogin failed , Error code %d.\n",ret);
		goto exit; //登录失败，退出登录
    }
    
    //play_sound("../../bin/wav/weather.pcm");  
	//run_iat("../../bin/wav/weather.pcm", session_begin_params_sch, session_begin_params_tts);

	 printf("开始合成 ...\n");
	
     ret = text_to_speech(text, sr_filename, session_begin_params_tts);
   	 if (MSP_SUCCESS != ret)
     {
    	 printf("text_to_speech failed, error code: %d.\n", ret);
     }

	ret = text_to_speech(repeat_text, rp_filename, session_begin_params_tts);
        if (MSP_SUCCESS != ret)
        {

         printf("text_to_speech failed, error code: %d.\n", ret);

        }

	ret = text_to_speech(beyond_text, by_filename, session_begin_params_tts);
        if (MSP_SUCCESS != ret)
        {

         printf("text_to_speech failed, error code: %d.\n", ret);

        }

     printf("合成完毕\n");

     play_sound("schh.wav");   

    //system("play schh.wav");

	 while(1)
	{
	 //printf("Speak in 5 seconds\n");
	
	 demo_mic(session_begin_params);
     if(repeat)
     {
         run_iat("schh.wav", session_begin_params_sch, session_begin_params_tts);
     }
     else
     {
       play_sound("repeat.wav");

	   //system("play repeat.wav");
     }
  //printf("15 sec passed\n");	
	}

exit:
	printf("按任意键退出 ...\n");
	getchar();
	MSPLogout(); //退出登录

	return 0;
}
