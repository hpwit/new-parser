#include "string.h"
#include "vect.h"
void str_split( vect<char *> *result,char* a_str, char *delim)
{
	//vect<char *> result;
	//int count     = 0;
	char* tmp        = a_str;
	char* last_comma = NULL;


		tmp=a_str;
		//int n_count=0;
		last_comma=a_str;
		//count=0;
		while (*tmp)
		{
			if (strncmp(delim,tmp,strlen(delim))==0)
			{

				char *d=(char*)malloc(tmp-last_comma+1);
				strncpy(d,last_comma,tmp-last_comma);
				d[tmp-last_comma]=0;
				//result[n_count]=d;
                result->push_back(d);
				//n_count++;
				tmp+=strlen(delim);
				last_comma = tmp;
			}
			else
			{
				tmp++;
			}

		}
		if(last_comma < (a_str + strlen(a_str)))
		{
			char *d=(char*)malloc(tmp-last_comma+1);
			strncpy(d,last_comma,tmp-last_comma);
			d[tmp-last_comma]=0;
			//result[n_count]=d;
            result->push_back(d);
		}

	printf("qq %d\n",result->size());
	//return result;
}

//template<typename ... Args>
