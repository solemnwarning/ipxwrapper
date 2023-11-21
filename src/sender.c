#define WINSOCK_API_LINKAGE

#include <assert.h>
#include <utlist.h>
#include <winsock2.h>
#include <windows.h>

#include "common.h"
#include "ipxwrapper.h"
#include "sender.h"

#define SENDER_MAX_BUFFERS 32

#define SENDER_MAX_HEADER_SIZE 32
#define SENDER_MAX_BUFFER_SIZE 65536
#define SENDER_MAX_ADDR_SIZE sizeof(struct sockaddr_storage)

struct SenderBuffer
{
	unsigned char header_data[SENDER_MAX_HEADER_SIZE];
	unsigned int header_size;
	
	unsigned char *data;
	unsigned int data_buffer_size;
	unsigned int data_size;
	
	int sock;
	unsigned char addr[SENDER_MAX_ADDR_SIZE];
	unsigned int addrlen;
	
	WSABUF sendbufs[2];
	unsigned int num_sendbufs;
	
	WSAOVERLAPPED overlapped;
	SenderQueue *queue;
	
	struct SenderBuffer *prev;
	struct SenderBuffer *next;
};

typedef struct SenderBuffer SenderBuffer;

struct SenderQueue
{
	CRITICAL_SECTION lock;
	
	unsigned int total_buffers;
	
	SenderBuffer *free_buffers;
	HANDLE free_buffer_event;
	
	SenderBuffer *queued_buffers;
	HANDLE queued_buffer_event;
};

struct SenderThread
{
	SenderQueue *queue;
	HANDLE stop_thread_event;
	
	HANDLE thread;
};

static SenderBuffer *_SenderQueue_get_free_buffer(SenderQueue *queue);
static void _SenderQueue_put_free_buffer(SenderQueue *queue, SenderBuffer *buffer);
static SenderBuffer *_SenderQueue_get_queued_buffer(SenderQueue *queue);
static void _SenderQueue_put_queued_buffer(SenderQueue *queue, SenderBuffer *buffer);

static DWORD WINAPI _SenderThread_main(LPVOID lpParameter);
static void WINAPI _SenderThread_send_completion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped, DWORD dwFlags);

SenderQueue *SenderQueue_new(void)
{
	SenderQueue *self = malloc(sizeof(SenderQueue));
	if(self == NULL)
	{
		log_printf(LOG_ERROR, "Unable to allocate SenderQueue structure");
		return NULL;
	}
	
	self->total_buffers = 0;
	self->free_buffers = NULL;
	self->queued_buffers = NULL;
	
	if(!InitializeCriticalSectionAndSpinCount(&(self->lock), 0x80000000))
	{
		log_printf(LOG_ERROR, "Failed to initialise critical section: %s", w32_error(GetLastError()));
		
		free(self);
		return NULL;
	}
	
	self->free_buffer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(self->free_buffer_event == NULL)
	{
		log_printf(LOG_ERROR,
			"Unable to create free_buffer_event event object: %s",
			w32_error(GetLastError()));
		
		DeleteCriticalSection(&(self->lock));
		free(self);
		return NULL;
	}
	
	self->queued_buffer_event = CreateEvent(NULL, FALSE, FALSE, NULL);
	if(self->queued_buffer_event == NULL)
	{
		log_printf(LOG_ERROR,
			"Unable to create queued_buffer_event event object: %s",
			w32_error(GetLastError()));
		
		CloseHandle(self->free_buffer_event);
		DeleteCriticalSection(&(self->lock));
		free(self);
		return NULL;
	}
	
	return self;
}

void SenderQueue_destroy(SenderQueue *self)
{
	if(self == NULL)
	{
		return;
	}
	
	while(self->free_buffers != NULL)
	{
		SenderBuffer *buffer = self->free_buffers;
		
		DL_DELETE(self->free_buffers, buffer);
		free(buffer);
	}
	
	while(self->queued_buffers != NULL)
	{
		SenderBuffer *buffer = self->queued_buffers;
		
		DL_DELETE(self->queued_buffers, buffer);
		free(buffer);
	}
	
	CloseHandle(self->queued_buffer_event);
	CloseHandle(self->free_buffer_event);
	DeleteCriticalSection(&(self->lock));
	free(self);
}

bool SenderQueue_send(struct SenderQueue *queue, int sock, const void *header_data, unsigned int header_size, const void *data, unsigned int data_size, const struct sockaddr *addr, int addrlen)
{
	assert(header_size <= SENDER_MAX_HEADER_SIZE);
	assert(data_size <= SENDER_MAX_BUFFER_SIZE);
	assert(addrlen <= SENDER_MAX_ADDR_SIZE);
	
	SenderBuffer *buffer = _SenderQueue_get_free_buffer(queue);
	if(!buffer)
	{
		return false;
	}
	
	if(buffer->data_buffer_size < data_size)
	{
		void *newbuf = realloc(buffer->data, data_size);
		if(newbuf == NULL)
		{
			log_printf(LOG_ERROR, "Unable to allocate %u byte data buffer", data_size);
			
			_SenderQueue_put_free_buffer(queue, buffer);
			return false;
		}
		
		buffer->data = newbuf;
		buffer->data_buffer_size = data_size;
	}
	
	buffer->sock = sock;
	
	memcpy(buffer->header_data, header_data, header_size);
	buffer->header_size = header_size;
	
	memcpy(buffer->data, data, data_size);
	buffer->data_size = data_size;
	
	memcpy(buffer->addr, addr, addrlen);
	buffer->addrlen = addrlen;
	
	buffer->num_sendbufs = 0;
	
	if(buffer->header_size > 0)
	{
		buffer->sendbufs[buffer->num_sendbufs].buf = (char*)(buffer->header_data);
		buffer->sendbufs[buffer->num_sendbufs].len = buffer->header_size;
		
		++(buffer->num_sendbufs);
	}
	
	buffer->sendbufs[buffer->num_sendbufs].buf = (char*)(buffer->data);
	buffer->sendbufs[buffer->num_sendbufs].len = buffer->data_size;
	++(buffer->num_sendbufs);
	
	_SenderQueue_put_queued_buffer(queue, buffer);
	
	return true;
}

static SenderBuffer *_SenderQueue_get_free_buffer(SenderQueue *self)
{
	SenderBuffer *buffer = NULL;
	
	while(buffer == NULL)
	{
		EnterCriticalSection(&(self->lock));
		
		if(self->free_buffers != NULL)
		{
			buffer = self->free_buffers;
			DL_DELETE(self->free_buffers, buffer);
		}
		else if(self->total_buffers < SENDER_MAX_BUFFERS)
		{
			/* Allocate a new buffer. */
			
			buffer = malloc(sizeof(SenderBuffer));
			if(buffer == NULL)
			{
				LeaveCriticalSection(&(self->lock));
				return NULL;
			}
			
			buffer->data = NULL;
			buffer->data_buffer_size = 0;
			
			buffer->queue = self;
			
			buffer->prev = NULL;
			buffer->next = NULL;
			
			++(self->total_buffers);
		}
		
		LeaveCriticalSection(&(self->lock));
		
		if(buffer == NULL)
		{
			/* wait for buffer to be available */
			WaitForSingleObject(self->free_buffer_event, INFINITE);
		}
	}
	
	return buffer;
}

static void _SenderQueue_put_free_buffer(SenderQueue *queue, SenderBuffer *buffer)
{
	EnterCriticalSection(&(queue->lock));
	DL_APPEND(queue->free_buffers, buffer);
	LeaveCriticalSection(&(queue->lock));
	
	SetEvent(queue->free_buffer_event);
}

static SenderBuffer *_SenderQueue_get_queued_buffer(SenderQueue *queue)
{
	EnterCriticalSection(&(queue->lock));
	
	SenderBuffer *buffer = NULL;
	
	if(queue->queued_buffers != NULL)
	{
		buffer = queue->queued_buffers;
		DL_DELETE(queue->queued_buffers, buffer);
	}
	
	LeaveCriticalSection(&(queue->lock));
	
	return buffer;
}

static void _SenderQueue_put_queued_buffer(SenderQueue *queue, SenderBuffer *buffer)
{
	EnterCriticalSection(&(queue->lock));
	DL_APPEND(queue->queued_buffers, buffer);
	LeaveCriticalSection(&(queue->lock));
	
	SetEvent(queue->queued_buffer_event);
}

SenderThread *SenderThread_new(SenderQueue *queue)
{
	SenderThread *self = malloc(sizeof(SenderThread));
	if(self == NULL)
	{
		log_printf(LOG_ERROR, "Unable to allocate SenderThread structure\n");
		return NULL;
	}
	
	self->queue = queue;
	
	self->stop_thread_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	if(self->stop_thread_event == NULL)
	{
		log_printf(LOG_ERROR,
			"Unable to create stop_thread_event event object: %s",
			w32_error(GetLastError()));
		
		free(self);
		return NULL;
	}
	
	self->thread = CreateThread(
		NULL,                 /* lpThreadAttributes */
		0,                    /* dwStackSize */
		&_SenderThread_main,  /* lpStartAddress */
		self,                 /* lpParameter */
		0,                    /* dwCreationFlags */
		NULL);                /* lpThreadId */
	
	if(self->thread == NULL)
	{
		log_printf(LOG_ERROR,
			"Unable to create SenderThread thread: %s",
			w32_error(GetLastError()));
		
		CloseHandle(self->stop_thread_event);
		free(self);
		
		return NULL;
	}
	
	return self;
}

void SenderThread_destroy(SenderThread *self)
{
	if(self == NULL)
	{
		return;
	}
	
	SetEvent(self->stop_thread_event);
	WaitForSingleObject(self->thread, INFINITE);
	
	CloseHandle(self->thread);
	CloseHandle(self->stop_thread_event);
	free(self);
}

static DWORD WINAPI _SenderThread_main(LPVOID lpParameter)
{
	SenderThread *self = (SenderThread*)(lpParameter);
	
	while(true)
	{
		HANDLE handles[] = {
			self->stop_thread_event,
			self->queue->queued_buffer_event,
		};
		
		//DWORD result = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
		DWORD result = WaitForMultipleObjectsEx(2, handles, FALSE, INFINITE, TRUE);
		
		if(result == WAIT_OBJECT_0)
		{
			break;
		}
		else if(result == (WAIT_OBJECT_0 + 1))
		{
			log_printf(LOG_DEBUG, "_SenderThread_main woke to send packet");
			
			SenderBuffer *buffer;
			
			while((buffer = _SenderQueue_get_queued_buffer(self->queue)) != NULL)
			{
				log_printf(LOG_DEBUG, "_SenderThread_main sending a packet");
				
				/* TODO: Investigate using overlapped I/O */
				
				memset(&(buffer->overlapped), 0, sizeof(buffer->overlapped));
				buffer->overlapped.hEvent = buffer;
				
				DWORD bytes_sent;
				int send_result = r_WSASendTo(buffer->sock, buffer->sendbufs, buffer->num_sendbufs, &bytes_sent, 0, (struct sockaddr*)(buffer->addr), buffer->addrlen, &(buffer->overlapped), &_SenderThread_send_completion);
				
				if(send_result != 0)
				{
					DWORD error = WSAGetLastError();
					
					if(error == WSA_IO_PENDING)
					{
						/* Send operation is running asyncronously. */
						continue;
					}
					else{
						log_printf(LOG_ERROR, "WSASendTo failed (error code %u)", (unsigned)(WSAGetLastError()));
					}
				}
				
				_SenderQueue_put_free_buffer(self->queue, buffer);
			}
		}
		else if(result == WAIT_IO_COMPLETION)
		{
			/* no-op */
		}
		else{
			log_printf(LOG_ERROR, "_SenderThread_main result %u GetLastError %u", (unsigned)(result), (unsigned)(GetLastError()));
		}
	}
	
	return 0;
}

static void WINAPI _SenderThread_send_completion(DWORD dwErrorCode, DWORD dwNumberOfBytesTransfered, LPOVERLAPPED lpOverlapped, DWORD dwFlags)
{
	SenderBuffer *buffer = (SenderBuffer*)(lpOverlapped->hEvent);
	
	if(dwErrorCode != 0)
	{
		log_printf(LOG_ERROR, "WSASendTo failed (error code %u)", (unsigned)(dwErrorCode));
	}
	
	_SenderQueue_put_free_buffer(buffer->queue, buffer);
}
