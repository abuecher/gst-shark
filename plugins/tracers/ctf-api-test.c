/* gcc -o ctf_app ctf-api-test.c */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <gst/gst.h>

#include <glib/gstdio.h>

#define BYTE_ORDER_LE (1)

typedef struct {
    FILE * metadata;
    FILE * datastream;
    GstClockTime tracer_start_time;
} tracer_struct;
    

typedef enum {
    TIMER_INIT_EVENT_ID,
    CPUUSAGE_EVENT_ID,
    PROCTIME_EVENT_ID,
    LATENCY_EVENT_ID,
    FPS_EVENT_ID,
    PIPELINE_GRAPH_EVENT_ID,
    SCHED_TIME_EVENT_ID,
    
} event_id;

tracer_struct tracer;

int Magic = 0xC1FC1FC1; // 0xc11ffcc1
char UUID[] = {0xd1,0x8e,0x63,0x74,0x35,0xa1,0xcd,0x42,0x8e,0x70,0xa9,0xcf,0xfa,0x71,0x27,0x93};
//~ char UUID[] = {0x67,0xE1,0x89,0xCE,0xA4,0xA0,0x49,0x04,0x83,0x70,0x3B,0xFE,0xFF,0xF3,0x6A,0xDC};
char * UUIDstring = "d18e6374-35a1-cd42-8e70-a9cffa712793";

/* Metadata format string */
static const char metadata_fmt2[] = "\
/* CTF 1.8 */\n\
typealias integer { size = 8; align = 8; signed = false; } := uint8_t;\n\
typealias integer { size = 16; align = 8; signed = false; } := uint16_t;\n\
typealias integer { size = 32; align = 8; signed = false; } := uint32_t;\n\
typealias integer { size = 64; align = 8; signed = false; } := uint64_t;\n\
typealias integer { size = 5; align = 1; signed = false; } := uint5_t;\n\
typealias integer { size = 27; align = 1; signed = false; } := uint27_t;\n\
\n\
trace {\n\
	major = %u;\n\
	minor = %u;\n\
	uuid = \"%s\";\n\
	byte_order = %s;\n\
	packet.header := struct {\n\
		uint32_t magic;\n\
		uint8_t  uuid[16];\n\
		uint32_t stream_id;\n\
	};\n\
};\n\
\n\
\n\
typealias integer {\n\
	size = 32; align = 8; signed = false;\n\
	map = clock.monotonic.value;\n\
} := uint32_clock_monotonic_t;\n\
\n\
typealias integer {\n\
	size = 64; align = 8; signed = false;\n\
	map = clock.monotonic.value;\n\
} := uint64_clock_monotonic_t;\n\
\n\
struct packet_context {\n\
	uint64_clock_monotonic_t timestamp_begin;\n\
	uint64_clock_monotonic_t timestamp_end;\n\
	uint32_t events_discarded;\n\
};\n\
\n\
struct event_header_compact {\n\
	enum : uint5_t { compact = 0 ... 30, extended = 31 } id;\n\
	variant <id> {\n\
		struct {\n\
			uint27_t timestamp;\n\
		} compact;\n\
		struct {\n\
			uint32_t id;\n\
			uint64_t timestamp;\n\
		} extended;\n\
	} v;\n\
} align(8);\n\
\n\
struct event_header_large {\n"
#if 0
"	uint16_t id;\n\
   uint32_t timestamp;"
#else
"	enum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n\
	variant <id> {\n\
		struct {\n\
			uint32_t timestamp;\n\
		} compact;\n\
		struct {\n\
			uint32_t id;\n\
			uint64_t timestamp;\n\
		} extended;\n\
	} v;\n"
#endif
"} align(8);\n\
\n\
stream {\n\
	id = 0;\n\
	event.header := struct event_header_large;\n\
	packet.context := struct packet_context;\n\
};\n\
\n\
event {\n\
	name = timer_init;\n\
	id = 0;\n\
	stream_id = 0;\n\
	fields := struct {\n\
		integer { size = 32; align = 8; signed = 0; encoding = none; base = 10; } _timer;\n\
	};\n\
};\n\
";



static const char metadata_fmt[] =
"/* CTF 1.8 */\n"
"typealias integer { size = 8; align = 8; signed = false; } := uint8_t;\n"
"typealias integer { size = 16; align = 16; signed = false; } := uint16_t;\n"
"typealias integer { size = 32; align = 32; signed = false; } := uint32_t;\n"
"typealias integer { size = 64; align = 64; signed = false; } := uint64_t;\n"
"\n"
"trace {\n"
"	major = %u;\n"			/* major (e.g. 0) */
"	minor = %u;\n"			/* minor (e.g. 1) */
"	uuid = \"%s\";\n"		/* UUID */
"	byte_order = %s;\n"		/* be or le */
"	packet.header := struct {\n"
"		uint32_t magic;\n"
"		uint8_t  uuid[16];\n"
"	};\n"
"};\n"
"\n"
"stream {\n"
"	packet.context := struct {\n"
//~ "		uint64_t content_size;\n"
//~ "		uint64_t packet_size;\n"
"	};\n"
"	typealias integer { size = 64; align = 64; signed = false; } := uint64_t;\n"
"	event.header := struct {\n"
//~ "		enum : uint16_t { compact = 0 ... 65534, extended = 65535 } id;\n"
"		uint64_t timestamp;\n"
"	};\n"					/* Stream event header (opt.) */
"};\n"
"\n";


static const char cpuusage_metadata_event_header[] =
"event {\n\
	name = cpuusage;\n\
	id = %d;\n\
	fields := struct {\n\
		integer { size = 32; align = 8; signed = 0; encoding = none; base = 10; } _num;\n\
		integer { size = 64; align = 8; signed = 0; encoding = none; base = 10; } _usage;\n\
	};\n\
};\n\
\n";


static const char proctime_metadata_event_header[] = "\
event {\n\
        name = proctime;\n\
        id = %d;\n\
        stream_id = 0;\n\
        fields := struct {\n\
                integer { size = 8; align = 8; signed = 1; encoding = UTF8; base = 10; } _element[16];\n\
                integer { size = 64; align = 8; signed = 0; encoding = none; base = 10; } _time;\n\
        };\n\
};\n\
";

static const char fps_metadata_event_header[] = "\
event {\n\
        name = framerate;\n\
        id = %d;\n\
        stream_id = 0;\n\
        fields := struct {\n\
                integer { size = 8; align = 8; signed = 1; encoding = UTF8; base = 10; } _element[16];\n\
                integer { size = 32; align = 8; signed = 0; encoding = none; base = 10; } _fps;\n\
        };\n\
};\n\
";

void TracerInit(tracer_struct * tracer)
{
    tracer->metadata = fopen("metadata","w");

    tracer->datastream = fopen("datastream","w");
    
    tracer->tracer_start_time = gst_util_get_timestamp();
}

void TracerFinalize(tracer_struct * tracer)
{
    fclose(tracer->metadata);
    fclose(tracer->datastream);
}


/*
 * Write event packet header
 */
void CTFDataStreamGenerate(tracer_struct * tracer, char * UUID,int UUID_size, uint64_t content_size,uint64_t packet_size )
{
    FILE *fd;
    uint64_t time_stamp_begin;
    uint64_t time_stamp_end;
    uint32_t events_discarted;
    //~ uint32_t cpi_id;
    uint32_t stream_id;
    
    fd = tracer->datastream;
    /* http://diamon.org/ctf/,
     * Section 5. Event packet header
     */
     /* Magic number */
    fwrite(&Magic,sizeof(char),sizeof(Magic),fd);
    /* Trace UUID */
    fwrite(UUID,sizeof(char),UUID_size,fd);
    /* Stream ID (optional) */
    stream_id = 0;
    fwrite(&stream_id,sizeof(char),sizeof(uint32_t),fd);
    /*
     * Section 5. Event packet context
     */
     
    /* Time Stamp begin */
    time_stamp_begin = 0x3e3db41faf8; // 0xf8fa41dbe3030000
    //~ time_stamp_begin = 0xAAAAAAAAAAAAAAAA;
    fwrite(&time_stamp_begin,sizeof(char),sizeof(uint64_t),fd);
    /* Time Stamp end */
    time_stamp_end = 0x000003e3ec8152ee;// 0xee5281ece3030000;
    //~ time_stamp_end = 0xBBBBBBBBBBBBBBBB;
    fwrite(&time_stamp_end,sizeof(char),sizeof(uint64_t),fd);
    /* Events discarted */
    //~ events_discarted = 0xCCCCCCCC;
    events_discarted = 0x0;
    fwrite(&events_discarted,sizeof(char),sizeof(uint32_t),fd);
    
    //~ cpi_id = 0xDDDDDDDD;
    //~ cpi_id = 0x0;
    //~ fwrite(&cpi_id,sizeof(char),sizeof(uint32_t),fd);
    
    /* Content size */
    content_size = 0x000003e800000000;
    //~ fwrite(&content_size,sizeof(char),sizeof(uint64_t),fd);
    /* Packet size */
    packet_size = 0x0004000000000000;  /* 262144 = 0x40000 */
    //~ fwrite(&packet_size,sizeof(char),sizeof(uint64_t),fd);
    /* ? ? ? ? */
    uint32_t unknown = 0x0000FFFF;
    fwrite(&unknown,sizeof(char),sizeof(uint32_t),fd);
}

void CTFDataStreamPading(FILE *fd)
{
    int pad_num = 32640;
    int zero = 0;
    for (;pad_num > 0; --pad_num)
    {
        fwrite(&zero,sizeof(char),1,fd);
    }
}


void CTFMetadataGenerate(tracer_struct * tracer, int major, int minor, char * UUID, int byte_order)
{
    /* Convert UUID */
    //~ bt_uuid_unparse(UUID, UUIDstring);
    
    fprintf(tracer->metadata, metadata_fmt2,
    major, /* major */
    minor, /* minor */
    UUIDstring,
    byte_order ? "le" : "be"
    );
}


void CTFMetadataAddEvent(tracer_struct * tracer, const char* metadata_event, event_id id)
{
    fprintf(tracer->metadata, metadata_event,
    id
    );
}

void CTFNewCpuUsageEvent(tracer_struct * tracer, int16_t event_id, uint32_t timestamp, int32_t num, int64_t usage)
{
    FILE *fd;
    
    fd = tracer->datastream;
        //~ int size = strlen(msg);
    /* Add event ID*/
    fwrite(&event_id,sizeof(char),sizeof(int16_t),fd);
    fwrite(&timestamp,sizeof(char),sizeof(uint32_t),fd);
    
    fwrite(&num,sizeof(char),sizeof(uint32_t),fd);
    
    fwrite(&usage,sizeof(char),sizeof(uint64_t),fd);
}


void CTFNewProcTimeEvent(tracer_struct * tracer, int16_t event_id, uint32_t timestamp, char * element, int64_t time)
{
    FILE *fd;
    
    fd = tracer->datastream;
    fwrite(&event_id,sizeof(char),sizeof(int16_t),fd);

    fwrite(&timestamp,sizeof(char),sizeof(uint32_t),fd);
    
    /*************************************************/
    int size = strlen(element);

    fwrite(element,sizeof(char),size+1,fd);

    /* Verify if padding must be added */
    int pad_num = (size+1)%16;
    char zero=0;
    if (pad_num != 0)
    {
        pad_num = 16 - pad_num;

        for (;pad_num > 0; --pad_num)
        {
            fwrite(&zero,sizeof(char),1,fd);
        }
    }
    /*************************************************/

    fwrite(&time,sizeof(char),sizeof(int64_t),fd);
}

void CTFNewFPSEvent(tracer_struct * tracer, int16_t event_id, uint32_t timestamp, char * element, int32_t fps)
{
    FILE *fd;
    
    fd = tracer->datastream;
    fwrite(&event_id,sizeof(char),sizeof(int16_t),fd);

    fwrite(&timestamp,sizeof(char),sizeof(uint32_t),fd);
    
    /*************************************************/
    int size = strlen(element);

    fwrite(element,sizeof(char),size+1,fd);

    /* Verify if padding must be added */
    int pad_num = (size+1)%16;
    char zero=0;
    if (pad_num != 0)
    {
        pad_num = 16 - pad_num;

        for (;pad_num > 0; --pad_num)
        {
            fwrite(&zero,sizeof(char),1,fd);
        }
    }
    /*************************************************/

    fwrite(&fps,sizeof(char),sizeof(int32_t),fd);
}


void CTFNewTimerInitEvent(tracer_struct * tracer, int16_t event_id, uint32_t timestamp, int32_t timer)
{
    //~ int size = strlen(msg);
    /* Add event ID*/
    int32_t unknown;
    FILE * fd;
    
    fd = tracer->datastream;
    
    fwrite(&event_id,sizeof(char),sizeof(int16_t),fd);

    fwrite(&timestamp,sizeof(char),sizeof(uint32_t),fd);
    
    //~ unknown = 0x000003e3;
    unknown = 0;
    fwrite(&unknown,sizeof(char),sizeof(uint32_t),fd);
    
    fwrite(&timer,sizeof(char),sizeof(uint32_t),fd);

}

int main (int argc, char *argv[])
{
    //~ FILE* fd;
    //~ FILE* FDMetadata;
    
    TracerInit(&tracer);
    
    gst_init (&argc, &argv);

    /* Create metadata file */
#if 1
    //~ FDMetadata = fopen("metadata", "w");
    
    

    CTFMetadataGenerate(&tracer, 1, 3, UUID, BYTE_ORDER_LE);
    
    /* Add event descriptor */
    CTFMetadataAddEvent(&tracer,cpuusage_metadata_event_header,CPUUSAGE_EVENT_ID);
    CTFMetadataAddEvent(&tracer,proctime_metadata_event_header,PROCTIME_EVENT_ID);
    CTFMetadataAddEvent(&tracer,fps_metadata_event_header,FPS_EVENT_ID);

    //~ fclose(FDMetadata);

#endif

    /* Create datastream file */

    //~ fd = fopen("datastream", "w");

    CTFDataStreamGenerate(&tracer, UUID,sizeof(UUID),0,0);

    /* Add events */
    CTFNewTimerInitEvent(&tracer, 0, 0, 0);
    
    CTFNewCpuUsageEvent(&tracer, CPUUSAGE_EVENT_ID, 10, 0, 11);
    CTFNewCpuUsageEvent(&tracer, CPUUSAGE_EVENT_ID, 20, 1, 22);
    CTFNewCpuUsageEvent(&tracer, CPUUSAGE_EVENT_ID, 30, 2, 33);
    CTFNewCpuUsageEvent(&tracer, CPUUSAGE_EVENT_ID, 40, 3, 44);
    
    
    CTFNewProcTimeEvent(&tracer, PROCTIME_EVENT_ID, 50, "identity0", 1000);
    CTFNewProcTimeEvent(&tracer, PROCTIME_EVENT_ID, 60, "queue0", 100);
    CTFNewProcTimeEvent(&tracer, PROCTIME_EVENT_ID, 70, "identity1", 1500);
    
    CTFNewFPSEvent(&tracer, FPS_EVENT_ID, 100, "filesrc0", 15);
    
    TracerFinalize(&tracer);
    
    
    /******************************************************************/
#if 1
    GstClockTime time;
    GST_ERROR ("ERROR");
    time = gst_util_get_timestamp ();
    g_printf ("%" GST_TIME_FORMAT"\n", GST_TIME_ARGS (time));
#endif

    return 0;
}