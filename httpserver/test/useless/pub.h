#ifndef PUB_H
#define PUB_H

int setnonbloking(int fd);
void addfd(int epollfd,int fd,bool one_shot);
void modfd(int epollfd,int fd,int ev);
#endif
