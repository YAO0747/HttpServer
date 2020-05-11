#ifndef LOCK_H
#define LOCK_H

#include<exception>
#include<pthread.h>
#include<semaphore.h>

//信号量
class sem
{
private:
	sem_t my_sem;
public:
	sem()
	{
		if(sem_init(&my_sem,0,0)!=0)
			throw std::exception();
	}
	~sem()
	{
		sem_destroy(&my_sem);
	}
	//down
	bool wait()
	{
		return sem_wait(&my_sem)==0;
	}
	//up
	bool post()
	{
		return sem_post(&my_sem)==0;
	}
};

//互斥量
class locker
{
private:
	pthread_mutex_t m_mutex;
public:
	locker()
	{
		if(pthread_mutex_init(&m_mutex,NULL)!=0)
			throw std::exception();
	}
	~locker()
	{
		pthread_mutex_destroy(&m_mutex);
	}
	bool lock()
	{
		return pthread_mutex_lock(&m_mutex)==0;
	}
	bool unlock()
	{
		return pthread_mutex_unlock(&m_mutex)==0;
	}
};


//条件变量
class cond
{
private:
	pthread_mutex_t m_mutex;//辅助条件变量的wait操作
	pthread_cond_t m_cond;
public:
	cond()
	{
		if(pthread_mutex_init(&m_mutex,NULL)!=0)
		{
			throw std::exception();
		}
		if(pthread_cond_init(&m_cond,NULL)!=0)
		{
			pthread_mutex_destroy(&m_mutex);
			throw std::exception();
		}
	}

	~cond()
	{
		pthread_mutex_destroy(&m_mutex);
		pthread_cond_destory(&m_cond);
	}
	
	//调用wait时会进入睡眠，直到其他线程调用了相应的signal
	bool wait()
	{
		int ret = 0;
		pthread_mutex_lock(&m_mutex);
		ret = pthread_cond_wait(&m_cond,&mutex);
		pthread_mutex_unlock(&mutex);
		return ret == 0;
	}

	bool signal()
	{
		return pthread_cond_signal(&m_cond)==0;
	}
	
};
#endif
