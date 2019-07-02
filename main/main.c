/**
  ******************************************************************************
  * @file    		main.c
  * @author  		BruceOu
  * @version 		V1.0
  * @date    		2019.01.24
  * @brief
  ******************************************************************************
  */
/**Includes*********************************************************************/
#include "app.h"
#include "V2X_Thread.h"

/**【全局变量声明】*************************************************************/
//线程号
pthread_t V2X_ThreadID;


/**
  * @brief     主函数
  * @param     argc
               argv
  * @retval    None
  */
int main(int argc, char *argv[])
{
	int ret = -ENOSYS;

	/*================== V2XTx模块 =====================*/
	ret = pthread_create(&V2X_ThreadID, NULL, V2X_Thread, NULL);
	if (ret)
	{
		errno = ret;
		perror("V2X Thread fail");
		exit(EXIT_FAILURE);
	}
	else
	{
		printf("create V2X Thread success\n");
	}


	while(1)
	{
		printf("[main]query spat message.\n");
        //延时100ms
        UtilNap(200*1000);
	}

    exit(EXIT_SUCCESS);
}