#include<stdio.h>
void counting(int a[] ,int,int );
void main()
{

    int a[20],i,j,n,k=5;
    printf("enter the no of element");
    scanf("%d",&n);
    printf("enter the element");
    for(i=1;i<=n;i++)
    {
        scanf("%d",&a[i]);
    }
    counting(a ,n,k );
    }
   void counting(int a[] ,int n,int k )
{
int b[20],i,j,p;
    int c[5]={0};
       for(i=1;i<=n;i++)
    {
        c[a[i]]++;
    }
    for(p=2;p<=k;p++)
    {
        c[p]=c[p]+c[p-1];
    }
    for(j=n;j>=1;j--)
    {
        b[c[a[j]]]=a[j];
        c[a[j]]--;
    }
   for(i=1;i<=n;i++)
    {
         printf("  %d ",b[i]);
    }
}

