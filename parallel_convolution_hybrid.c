//
//  convolution.c
//
//
//  Created by Josep Lluis Lerida on 11/03/15.
//
// This program calculates the convolution for PPM images.
// The program accepts an PPM image file, a text definition of the kernel matrix and the PPM file for storing the convolution results.
// The program allows to define image partitions for processing large images (>500MB)
// The 2D image is represented by 1D vector for chanel R, G and B. The convolution is applied to each chanel separately.

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include "mpi.h"

// Estructura per emmagatzemar el contingut d'una imatge.
struct imagenppm{
    int altura;
    int ancho;
    char *comentario;
    int maxcolor;
    int P;
    int *R;
    int *G;
    int *B;
};
typedef struct imagenppm* ImagenData;

// Estructura per emmagatzemar el contingut d'un kernel.
struct structkernel{
    int kernelX;
    int kernelY;
    float *vkern;
};
typedef struct structkernel* kernelData;

//Functions Definition
ImagenData initimage(char* nombre, FILE **fp, int partitions, int halo);
ImagenData duplicateImageData(ImagenData src, int partitions, int halo);

int readImage(ImagenData Img, FILE **fp, int dim, int halosize, long int *position);
int duplicateImageChunk(ImagenData src, ImagenData dst, int dim);
int initfilestore(ImagenData img, FILE **fp, char* nombre, long *position);
int savingChunk(ImagenData img, FILE **fp, int dim, int offset);
int convolve2D(int* inbuf, int* outbuf, int sizeX, int sizeY, float* kernel, int ksizeX, int ksizeY);
void freeImagestructure(ImagenData *src);

void master_job(int size, MPI_Status *status,  int *configArr, int n_chunks,  struct imagenppm *source,
                 struct imagenppm *output, int restWidthChunk, int widthChunk,  int *receiveArray,
                 int *sendArray, int existRestChunk, int chunk, int computedChunks, int chunkPosition,
                int endedThreads);

void
ask_for_work_and_make_it(MPI_Status *status, int partitions, int n_chunks,  struct structkernel *kern, int width,
                         int heightChunk, int restHeightChunk, int restWidthChunk, int widthChunk, int c,
                          int *receiveArray,  int *sendArray,  int *restArray);

//Open Image file and image struct initialization
ImagenData initimage(char* nombre, FILE **fp,int partitions, int halo){
    char c;
    char comentario[300];
    int i=0,chunk=0;
    ImagenData img=NULL;

    /*Se habre el fichero ppm*/

    if ((*fp=fopen(nombre,"r"))==NULL){
        perror("Error: ");
    }
    else{
        //Memory allocation
        img=(ImagenData) malloc(sizeof(struct imagenppm));

        //Reading the first line: Magical Number "P3"
        fscanf(*fp,"%c%d ",&c,&(img->P));

        //Reading the image comment
        while((c=fgetc(*fp))!= '\n'){comentario[i]=c;i++;}
        comentario[i]='\0';
        //Allocating information for the image comment
        img->comentario = calloc(strlen(comentario),sizeof(char));
        strcpy(img->comentario,comentario);
        //Reading image dimensions and color resolution
        fscanf(*fp,"%d %d %d",&img->ancho,&img->altura,&img->maxcolor);
        chunk = img->ancho*img->altura / partitions;
        //We need to read an extra row.
        chunk = chunk + img->ancho * halo;
        if ((img->R=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
        if ((img->G=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
        if ((img->B=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    }
    return img;
}

//Duplicate the Image struct for the resulting image
ImagenData duplicateImageData(ImagenData src, int partitions, int halo){
    char c;
    char comentario[300];
    unsigned int imageX, imageY;
    int i=0, chunk=0;
    //Struct memory allocation
    ImagenData dst=(ImagenData) malloc(sizeof(struct imagenppm));

    //Copying the magic number
    dst->P=src->P;
    //Copying the string comment
    dst->comentario = calloc(strlen(src->comentario),sizeof(char));
    strcpy(dst->comentario,src->comentario);
    //Copying image dimensions and color resolution
    dst->ancho=src->ancho;
    dst->altura=src->altura;
    dst->maxcolor=src->maxcolor;
    chunk = dst->ancho*dst->altura / partitions;
    //We need to read an extra row.
    chunk = chunk + src->ancho * halo;
    if ((dst->R=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    if ((dst->G=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    if ((dst->B=calloc(chunk,sizeof(int))) == NULL) {return NULL;}
    return dst;
}

//Read the corresponding chunk from the source Image
int readImage(ImagenData img, FILE **fp, int dim, int halosize, long *position){
    int i=0, k=0,haloposition=0;
    if (fseek(*fp,*position,SEEK_SET))
        perror("Error: ");
    haloposition = dim-(img->ancho*halosize*2);
    for(i=0;i<dim;i++) {
        // When start reading the halo store the position in the image file
        if (halosize != 0 && i == haloposition) *position=ftell(*fp);
        fscanf(*fp,"%d %d %d ",&img->R[i],&img->G[i],&img->B[i]);
        k++;
    }
//    printf ("Readed = %d pixels, posicio=%lu\n",k,*position);
    return 0;
}

//Duplication of the  just readed source chunk to the destiny image struct chunk
int duplicateImageChunk(ImagenData src, ImagenData dst, int dim){
    int i=0;

    for(i=0;i<dim;i++){
        dst->R[i] = src->R[i];
        dst->G[i] = src->G[i];
        dst->B[i] = src->B[i];
    }
//    printf ("Duplicated = %d pixels\n",i);
    return 0;
}

// Open kernel file and reading kernel matrix. The kernel matrix 2D is stored in 1D format.
kernelData leerKernel(char* nombre){
    FILE *fp;
    int i=0;
    kernelData kern=NULL;

    /*Opening the kernel file*/
    fp=fopen(nombre,"r");
    if(!fp){
        perror("Error: ");
    }
    else{
        //Memory allocation
        kern=(kernelData) malloc(sizeof(struct structkernel));

        //Reading kernel matrix dimensions
        fscanf(fp,"%d,%d,", &kern->kernelX, &kern->kernelY);
        kern->vkern = (float *)malloc(kern->kernelX*kern->kernelY*sizeof(float));

        // Reading kernel matrix values
        for (i=0;i<(kern->kernelX*kern->kernelY)-1;i++){
            fscanf(fp,"%f,",&kern->vkern[i]);
        }
        fscanf(fp,"%f",&kern->vkern[i]);
        fclose(fp);
    }
    return kern;
}

// Open the image file with the convolution results
int initfilestore(ImagenData img, FILE **fp, char* nombre, long *position){
    /*Se crea el fichero con la imagen resultante*/
    if ( (*fp=fopen(nombre,"w")) == NULL ){
        perror("Error: ");
        return -1;
    }
    /*Writing Image Header*/
    fprintf(*fp,"P%d\n%s\n%d %d\n%d\n",img->P,img->comentario,img->ancho,img->altura,img->maxcolor);
    *position = ftell(*fp);
    return 0;
}

// Writing the image partition to the resulting file. dim is the exact size to write. offset is the displacement for avoid halos.
int savingChunk(ImagenData img, FILE **fp, int dim, int offset){
    int i,k=0;
    //Writing image partition
    for(i=offset;i<dim+offset;i++){
        fprintf(*fp,"%d %d %d ",img->R[i],img->G[i],img->B[i]);
//        if ((i+1)%6==0) fprintf(*fp,"\n");
        k++;
    }
//    printf ("Writed = %d pixels, dim=%d, offset=%d\n",k,dim, offset);
    return 0;
}

// This function free the space allocated for the image structure.
void freeImagestructure(ImagenData *src){

    free((*src)->comentario);
    free((*src)->R);
    free((*src)->G);
    free((*src)->B);

    free(*src);
}

///////////////////////////////////////////////////////////////////////////////
// 2D convolution
// 2D data are usually stored in computer memory as contiguous 1D array.
// So, we are using 1D array for 2D data.
// 2D convolution assumes the kernel is center originated, which means, if
// kernel size 3 then, k[-1], k[0], k[1]. The middle of index is always 0.
// The following programming logics are somewhat complicated because of using
// pointer indexing in order to minimize the number of multiplications.
//
//
// signed integer (32bit) version:
///////////////////////////////////////////////////////////////////////////////
int convolve2D(int* in, int* out, int dataSizeX, int dataSizeY,
               float* kernel, int kernelSizeX, int kernelSizeY)
{
    int i, j, m, n;
    int *inPtr, *inPtr2, *outPtr;
    float *kPtr;
    int kCenterX, kCenterY;
    int rowMin, rowMax;                             // to check boundary of input array
    int colMin, colMax;                             //
    float sum;                                      // temp accumulation buffer
    
    // check validity of params
    if(!in || !out || !kernel) return -1;
    if(dataSizeX <= 0 || kernelSizeX <= 0) return -1;
    
    // find center position of kernel (half of kernel size)
    kCenterX = (int)kernelSizeX / 2;
    kCenterY = (int)kernelSizeY / 2;
    
    // init working  pointers
    inPtr = inPtr2 = &in[dataSizeX * kCenterY + kCenterX];  // note that  it is shifted (kCenterX, kCenterY),
    outPtr = out;
    kPtr = kernel;
    int *initial_in = inPtr;
    int *initial_out = out;
    
    // start convolution
#pragma omp parallel num_threads(4) private(sum, i, rowMax, rowMin, j, m, n, colMax, colMin) firstprivate(kPtr, inPtr, inPtr2, outPtr)
{
    int id 	   = omp_get_thread_num();
    int numthreads = omp_get_num_threads();

    inPtr2 = inPtr2 + id;
    inPtr  = inPtr2;
    outPtr = outPtr + id;

    // start convolution
    for(i= 0; i < dataSizeY; ++i)                   // number of rows
    {
        // compute the range of convolution, the current row of kernel should be between these
        rowMax = i + kCenterY;
        rowMin = i - dataSizeY + kCenterY;

        for(j = id; j < dataSizeX; j+=numthreads)              // number of columns
        {
            // compute the range of convolution, the current column of kernel should be between these
            colMax = j + kCenterX;
            colMin = j - dataSizeX + kCenterX;

            sum = 0;                                // set to 0 before accumulate

            // flip the kernel and traverse all the kernel values
            // multiply each kernel value with underlying input data
            for(m = 0; m < kernelSizeY; ++m)        // kernel rows
            {
                // check if the index is out of bound of input array
                if(m <= rowMax && m > rowMin)
                {
                    for(n = 0; n < kernelSizeX; ++n)
                    {
                        // check the boundary of array
                        if(n <= colMax && n > colMin)
                            sum += *(inPtr - n) * *kPtr;

                        ++kPtr;                     // next kernel
                    }
                }
                else
                    kPtr += kernelSizeX;            // out of bound, move to next row of kernel

                inPtr -= dataSizeX;                 // move input data 1 raw up
            }

            // convert integer number
            if(sum >= 0) *outPtr = (int)(sum + 0.5f);
//            else *outPtr = (int)(sum - 0.5f)*(-1);
            // For using with image editors like GIMP or others...
            else *outPtr = (int)(sum - 0.5f);
            // For using with a text editor that read ppm images like libreoffice or others...
//            else *outPtr = 0;

            kPtr = kernel;                          // reset kernel to (0,0)

            inPtr2 = inPtr2 + numthreads;
            inPtr  = inPtr2;                       // next input
            outPtr = outPtr + numthreads;          // next output
        }
    }
}//End parallel
    return 0;
}


//////////////////////////////////////////////////////////////////////////////////////////////////
// MAIN FUNCTION
//////////////////////////////////////////////////////////////////////////////////////////////////
int main(int argc, char **argv)
{
    int i=0,j=0,k=0;
//    int headstored=0, imagestored=0, stored;
    //MPI
    int rank, size;
    MPI_Status status;
    MPI_Request send_request;
    int configArr[2];

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    if(argc != 6)
    {
        printf("Usage: mpiexec -n <threads> %s <image-file> <kernel-file> <result-file> <partitions> <chunks>\n", argv[0]);

        printf("\n\nError, Missing parameters:\n");
        printf("format: mpiexec -n threads ./convolutionMPI image_file kernel_file result_file chunks\n");
        printf("- image_file : source image path (*.ppm)\n");
        printf("- kernel_file: kernel path (text file with 1D kernel matrix)\n");
        printf("- result_file: result image path (*.ppm)\n");
        printf("- partitions : Image partitions\n");
        printf("- chunks : Number chunks\n\n");
        return -1;
    }

    //////////////////////////////////////////////////////////////////////////////////////////////////
    // READING IMAGE HEADERS, KERNEL Matrix, DUPLICATE IMAGE DATA, OPEN RESULTING IMAGE FILE
    //////////////////////////////////////////////////////////////////////////////////////////////////
    int imagesize, partitions, partsize, widthChunk, halo, halosize, n_chunks;
    long position=0;
    double start, tstart=0, tend=0, tread=0, tcopy=0, tconv=0, tstore=0, treadk=0;
    struct timeval tim;
    FILE *fpsrc=NULL,*fpdst=NULL;
    ImagenData source=NULL, output=NULL;
    double starttime, endtime;

    // Store number of partitions
    partitions = atoi(argv[4]);
    // Sotre number chunks
    n_chunks = atoi(argv[5]);
    ////////////////////////////////////////
    //Reading kernel matrix
    gettimeofday(&tim, NULL);
    start = tim.tv_sec+(tim.tv_usec/1000000.0);
    tstart = start;
    kernelData kern=NULL;
    if ( (kern = leerKernel(argv[2]))==NULL) {
        //        free(source);
        //        free(output);
        return -1;
    }
    //The matrix kernel define the halo size to use with the image. The halo is zero when the image is not partitioned.
    if (partitions==1) halo=0;
    else halo = (kern->kernelY/2)*2;
    gettimeofday(&tim, NULL);
    treadk = treadk + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

    if (rank == 0)
    {
        starttime = MPI_Wtime();
        printf("%f, ",starttime);
        ////////////////////////////////////////
        //Reading Image Header. Image properties: Magical number, comment, size and color resolution.
        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        //Memory allocation based on number of partitions and halo size.
        if ( (source = initimage(argv[1], &fpsrc, partitions, halo)) == NULL) {
            return -1;
        }
        gettimeofday(&tim, NULL);
        tread = tread + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

        //Duplicate the image struct.
        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        if ( (output = duplicateImageData(source, partitions, halo)) == NULL) {
            return -1;
        }
        gettimeofday(&tim, NULL);
        tcopy = tcopy + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

        ////////////////////////////////////////
        //Initialize Image Storing file. Open the file and store the image header.
        gettimeofday(&tim, NULL);
        start = tim.tv_sec+(tim.tv_usec/1000000.0);
        if (initfilestore(output, &fpdst, argv[3], &position)!=0) {
            perror("Error: ");
            //        free(source);
            //        free(output);
            return -1;
        }
        gettimeofday(&tim, NULL);
        tstore = tstore + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

        //////////////////////////////////////////////////////////////////////////////////////////////////
        // CHUNK READING
        //////////////////////////////////////////////////////////////////////////////////////////////////
        int c=0, offset=0;
        imagesize = source->altura*source->ancho;
        partsize  = (source->altura*source->ancho)/partitions;
        //    printf("%s ocupa %dx%d=%d pixels. Partitions=%d, halo=%d, partsize=%d pixels\n", argv[1], source->altura, source->ancho, imagesize, partitions, halo, partsize);
        while (c < partitions) {
            ////////////////////////////////////////////////////////////////////////////////
            //Reading Next chunk.
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if (c==0) {
                halosize  = halo/2;
                widthChunk = partsize + (source->ancho*halosize);
                offset   = 0;
            }
            else if(c<partitions-1) {
                halosize  = halo;
                widthChunk = partsize + (source->ancho*halosize);
                offset    = (source->ancho*halo/2);
            }
            else {
                halosize  = halo/2;
                widthChunk = partsize + (source->ancho*halosize);
                offset    = (source->ancho*halo/2);
            }
            //DEBUG
            //        printf("\nRound = %d, position = %ld, partsize= %d, widthChunk=%d pixels\n", c, position, partsize, widthChunk);

            if (readImage(source, &fpsrc, widthChunk, halo/2, &position)) {
                return -1;
            }
            gettimeofday(&tim, NULL);
            tread = tread + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

            //Duplicate the image chunk
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if ( duplicateImageChunk(source, output, widthChunk) ) {
                return -1;
            }
            //DEBUG
            //        for (i=0;i<widthChunk;i++)
            //            if (source->R[i]!=output->R[i] || source->G[i]!=output->G[i] || source->B[i]!=output->B[i]) printf("At position i=%d %d!=%d,%d!=%d,%d!=%d\n",i,source->R[i],output->R[i], source->G[i],output->G[i],source->B[i],output->B[i]);
            gettimeofday(&tim, NULL);
            tcopy = tcopy + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

            //////////////////////////////////////////////////////////////////////////////////////////////////
            // CHUNK CONVOLUTION
            //////////////////////////////////////////////////////////////////////////////////////////////////
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);

            //MPI
            // master code
            int heightChunk     = ( (source->altura/partitions) + halosize) / n_chunks;
            int restHeightChunk = ( (source->altura/partitions) + halosize) % n_chunks;
            int restWidthChunk  = restHeightChunk * source->ancho;
            int widthChunk      = heightChunk * source->ancho;

            //int receiveArray[(3 * widthChunk)], sendArray[(3 * widthChunk)], restArray[(3 * restWidthChunk)];
            int *receiveArray, *sendArray, *restArray;
            int existRestChunk = restWidthChunk == 0 ? 0 : 1;
            int chunk = 1, computedChunks = 0;
            int chunkPosition;
            int endedThreads = 0;

            receiveArray = (int*)malloc(sizeof(int) * (3 * widthChunk));
            sendArray = (int*)malloc(sizeof(int) * (3 * widthChunk));
            restArray = (int*)malloc(sizeof(int) * (3 * restWidthChunk));

            if (NULL == receiveArray || NULL == sendArray || NULL == restArray) {
                fprintf(stderr, "malloc failed\n");
                exit(1);
            }

            configArr[0] = source->ancho;
            configArr[1] = (source->altura / partitions) + halosize;

            master_job(size, &status, configArr, n_chunks, source, output, restWidthChunk, widthChunk, receiveArray,
                       sendArray, existRestChunk,
                       chunk, computedChunks, chunkPosition, endedThreads);


            gettimeofday(&tim, NULL);
            tconv = tconv + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);

            //////////////////////////////////////////////////////////////////////////////////////////////////
            // CHUNK SAVING
            //////////////////////////////////////////////////////////////////////////////////////////////////
            //Storing resulting image partition.
            gettimeofday(&tim, NULL);
            start = tim.tv_sec+(tim.tv_usec/1000000.0);
            if (savingChunk(output, &fpdst, partsize, offset)) {
                perror("Error: ");
                //        free(source);
                //        free(output);
                return -1;
            }
            gettimeofday(&tim, NULL);
            tstore = tstore + (tim.tv_sec+(tim.tv_usec/1000000.0) - start);
            //Next partition
            c++;
        }

        fclose(fpsrc);
        fclose(fpdst);

        //    freeImagestructure(&source);
        //    freeImagestructure(&output);

        gettimeofday(&tim, NULL);
        tend = tim.tv_sec+(tim.tv_usec/1000000.0);

        printf("%s, ", argv[1]);
        printf("%d, ", source->ancho);
        printf("%d, ", source->altura);
        printf("%d, ", kern->kernelX);
        printf("%d, ", kern->kernelY);
        printf("%.6lf, ", tread);
        printf("%.6lf, ", tcopy);
        printf("%.6lf, ", treadk);
        printf("%.6lf, ", tconv);
        printf("%.6lf, ", tstore);
        printf("%.6lf, ", tend-tstart);

        endtime = MPI_Wtime();
        printf("%f, ",endtime);
        printf("%f\n",endtime-starttime);

        freeImagestructure(&source);
        freeImagestructure(&output);
    }

    else // worker code
    {
        //Receive the widtht and heigh of the input image
        MPI_Bcast(configArr, 2, MPI_INT, 0, MPI_COMM_WORLD);

        int width = configArr[0], height = configArr[1];

        int heightChunk 	   = height / n_chunks;
        int restHeightChunk = height % n_chunks;
        int restWidthChunk  = restHeightChunk * width;
        int widthChunk      = heightChunk * width;
        int c = 0;
        int *receiveArray, *sendArray, *restArray;
        //int receiveArray[(3 * widthChunk)], sendArray[(3 * widthChunk)], restArray[(3 * restWidthChunk)];

        receiveArray = (int*)malloc(sizeof(int) * (3 * widthChunk));
        sendArray = (int*)malloc(sizeof(int) * (3 * widthChunk));
        restArray = (int*)malloc(sizeof(int) * (3 * restWidthChunk));

        if (NULL == receiveArray || NULL == sendArray || NULL == restArray) {
            fprintf(stderr, "malloc failed\n");
            exit(1);
        }

        ask_for_work_and_make_it(&status, partitions, n_chunks, kern, width, heightChunk, restHeightChunk, restWidthChunk,
                                 widthChunk, c, receiveArray,
                                 sendArray, restArray);
    } // end worker

    MPI_Finalize();
    return 0;
}

void
ask_for_work_and_make_it(MPI_Status *status, int partitions, int n_chunks, struct structkernel *kern, int width,
                         int heightChunk, int restHeightChunk, int restWidthChunk, int widthChunk, int c,
                         int *receiveArray, int *sendArray, int *restArray) {
    while (c < partitions)
        {
            while (1)
            {
                // ask master for work
                MPI_Send(sendArray, (3 * widthChunk), MPI_INT, 0, 0, MPI_COMM_WORLD);

                // recv response
                MPI_Recv(receiveArray, (3 * widthChunk), MPI_INT, 0, MPI_ANY_TAG, MPI_COMM_WORLD, status);


                if (((*status).MPI_TAG - 1) == -1) // -1 means no more
                {
                    break; // break the loop
                }

                else if ((*status).MPI_TAG == n_chunks + 1) //means that there are a remainder chunk
                {
                    convolve2D(receiveArray, restArray, width, restHeightChunk, kern->vkern, kern->kernelX, kern->kernelY);
                    convolve2D(receiveArray + restWidthChunk, restArray + restWidthChunk, width, restHeightChunk, kern->vkern, kern->kernelX, kern->kernelY);
                    convolve2D(receiveArray + 2 * restWidthChunk, restArray + 2 * restWidthChunk, width, restHeightChunk, kern->vkern, kern->kernelX, kern->kernelY);

                    MPI_Send(restArray, (3 * restWidthChunk), MPI_INT, 0, (*status).MPI_TAG, MPI_COMM_WORLD);

                    break; //It's the remainder part so there are no more info to process
                }

                else
                {
                    convolve2D(receiveArray, sendArray, width, heightChunk, kern->vkern, kern->kernelX, kern->kernelY);
                    convolve2D(receiveArray + widthChunk, sendArray + widthChunk, width, heightChunk, kern->vkern, kern->kernelX, kern->kernelY);
                    convolve2D(receiveArray + 2 * widthChunk, sendArray + 2 * widthChunk, width, heightChunk, kern->vkern, kern->kernelX, kern->kernelY);

                    MPI_Send(sendArray, (3 * widthChunk), MPI_INT, 0, (*status).MPI_TAG, MPI_COMM_WORLD);
                }
            }// end while
            c++;
        }
}

void master_job(int size, MPI_Status *status,  int *configArr, int n_chunks,  struct imagenppm *source,
        struct imagenppm *output, int restWidthChunk, int widthChunk,  int *receiveArray,
                int *sendArray, int existRestChunk, int chunk, int computedChunks, int chunkPosition,
                int endedThreads) {
    MPI_Bcast(configArr, 2, MPI_INT, 0, MPI_COMM_WORLD);


    while (chunk <= n_chunks || computedChunks < n_chunks || endedThreads < (size - 1) )
            {

                MPI_Recv(receiveArray, (3 * widthChunk), MPI_INT, MPI_ANY_SOURCE, MPI_ANY_TAG, MPI_COMM_WORLD, status);

                if (((*status).MPI_TAG - 1) == -1) // worker wants more work
                {
                    if (chunk > n_chunks)// There are no more work
                    {
                        if (existRestChunk == 1) //There are a little bit more work
                        {
                            chunkPosition = n_chunks * widthChunk;

                            memcpy(sendArray, source->R + chunkPosition, sizeof(int) * restWidthChunk );
                            memcpy(sendArray + restWidthChunk, source->G + chunkPosition, sizeof(int) * restWidthChunk );
                            memcpy(sendArray + 2 * restWidthChunk, source->B + chunkPosition, sizeof(int) * restWidthChunk );

                            MPI_Send(sendArray, (3 * restWidthChunk), MPI_INT, (*status).MPI_SOURCE, n_chunks + 1, MPI_COMM_WORLD);
                        }

                        else
                        {
                            MPI_Send(sendArray, (3 * widthChunk), MPI_INT, (*status).MPI_SOURCE, 0, MPI_COMM_WORLD);
                            endedThreads++;
                        }
                    }

                    else
                    {
                        chunkPosition = (chunk - 1) * widthChunk;

                        memcpy(sendArray, source->R + chunkPosition, sizeof(int) * widthChunk );
                        memcpy(sendArray + widthChunk, source->G + chunkPosition, sizeof(int) * widthChunk );
                        memcpy(sendArray + 2 * widthChunk, source->B + chunkPosition, sizeof(int) * widthChunk );

                        MPI_Send(sendArray, (3 * widthChunk), MPI_INT, (*status).MPI_SOURCE, chunk, MPI_COMM_WORLD);

                        chunk++;
                    }
                }

                else// worker wants to send finished work
                {

                    if ((*status).MPI_TAG == n_chunks + 1) //I'ts the remainder part
                    {
                        chunkPosition = n_chunks * widthChunk;

                        memcpy(output->R + chunkPosition, receiveArray, sizeof(int) * restWidthChunk);
                        memcpy(output->G + chunkPosition, receiveArray + restWidthChunk, sizeof(int) * restWidthChunk);
                        memcpy(output->B + chunkPosition, receiveArray + 2 * restWidthChunk, sizeof(int) * restWidthChunk);

                        existRestChunk = 0;
                        endedThreads++;
                    }

                    else
                    {
                        chunkPosition = ((*status).MPI_TAG - 1) * widthChunk;

                        memcpy(output->R + chunkPosition, receiveArray, sizeof(int) * widthChunk);
                        memcpy(output->G + chunkPosition, receiveArray + widthChunk, sizeof(int) * widthChunk);
                        memcpy(output->B + chunkPosition, receiveArray + 2 * widthChunk, sizeof(int) * widthChunk);

                        computedChunks++;
                    }
                }
            }//end while

}
