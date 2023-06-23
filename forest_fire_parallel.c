#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include "mpi.h"
#define N 1000 //grandezza foresta NxM
#define M 100   

#define P 50 //probabilità di creazione albero
#define F 20 //probabilità di incendio

// 0 vuoto, 1 rappresenta albero, 2 albero che sta bruciando

int rows, rank, size;
//FILE *file;

int** initForest(int **forest);
void igniteTrees(int **forest);
bool isFinished(int **forest);
void printForest(int **forest, int count);
void printForestFile(int **forest, int count, FILE *file);
int** fireExtention(int **forest);
void checkIntermediateRows(int **forest, int **newForest);
//correttezza
int isFinishedSequential(int **forest);
void extractFromFile(FILE *file, int iterazione);
int** fireExtentionSequential(int **forest);
int isEqual(int **forest, int pForest[][M]);
void correctness();

int main(int argc, char *argv[]){
    MPI_Init( &argc , &argv);
    MPI_Comm_size( MPI_COMM_WORLD , &size);
    MPI_Comm_rank( MPI_COMM_WORLD , &rank);
    int rest = N % size;
    int **forest;

    //char num[36];
    //sprintf(num, "./correctness/correctness%d.txt", rank);
    //file = fopen(num,"w");
    //if( file == NULL){
    //    printf("Errore apertura file %s\n", num);
    //    return 1;
    //}  

    double start, end;
    start = MPI_Wtime();

    if(N<1 || M<=0 || M>=1011){       //numero di colonne deve essere maggiore di 0 e minore di 1011 e il numero di righe maggiore di 2
        if(rank==0)
            printf("Errore grandezza della foresta\n");
        exit(1);
    }

    if(P<0 || P>100 || F<0 || F>100){
        printf("Errore valore probabilità non corretto\n");
        exit(1);
    }   

    if(N<size){
        if(rank==0)
            printf("Errore numero di processi maggiore di numero di righe\n");       // il programma richiede che il numero di processi deve essere maggiore del numero di righe
        exit(1);
    }

    if(rest > rank)
        rows = (N/size) +1;    // i primi processi con rank < rest processano una colonna in più
    else    
        rows = N/size;    

    forest = initForest(forest);
    igniteTrees(forest);

    bool finish = false;
    int count=0;

    while (!finish){
        if(rank==0)
            printf("iterazione %d\n", count);
        //printForestFile(forest, count, file);
        count++;
        forest = fireExtention(forest);
        finish = isFinished(forest);  
    }
    //printForestFile(forest, count, file);
    //printForest(forest,count);
    if(rank==0)
        if(finish)
            printf("forest non ha alberi accesi\n" );
        else
            printf("forest ha ancora alberi accesi\n" );
 
    MPI_Barrier(MPI_COMM_WORLD);
    end = MPI_Wtime();
    if(rank==0)
        printf("\n\ntempo trascorso: %f\n\n", end-start);
    
    for(int i=0; i<rows; i++)
        free(forest[i]);
    free(forest);

    //fclose(file);
    //if(rank==0)
    //    correctness();
    
    MPI_Finalize();
 
    return 0;
}

int** initForest(int **forest){
    srand(rank);
    forest = (int**) malloc(rows * sizeof(int*));
    for(int i=0; i<rows; i++)
        forest[i]= (int*) malloc(M * sizeof(int)); 
    int r; 
    for(int i=0;i<rows; i++)
        for(int j=0; j<M; j++){
            r = rand()%100;
            if(r>=0 && r<P)
                forest[i][j]= 1;
            else
                forest[i][j]= 0;
        }   
    return forest;
}

void igniteTrees(int **forest){
    srand(rank);

    int r; 
    for(int i=0;i<rows; i++)
        for(int j=0; j<M; j++)
            if(forest[i][j]==1){
                r = rand()%100;
                if(r>=0 && r<F)
                    forest[i][j] = 2;
            }   
}

bool isFinished(int **forest){
    bool *results;
    if(rank==0)
        results = (bool*) malloc(sizeof(bool)*size);

    bool result = true;
    for(int i=0; i< rows; i++)
        for(int j=0; j<M; j++)
            if(forest[i][j]==2){
                result = false;
                break;
            }

    MPI_Gather( &result , 1 , MPI_C_BOOL , results , 1 , MPI_C_BOOL , 0 , MPI_COMM_WORLD);  
    if(rank==0){
        result = true;
        for(int i=0; i<size; i++){
            if(results[i]==false)
                result = false;
        }
        free(results);
    }   
    //risultato di 0 viene condiviso a tutti i processi
    MPI_Bcast( &result , 1 , MPI_C_BOOL , 0 , MPI_COMM_WORLD);

    return result;   
}

void printForest(int **forest, int count){
    printf("processo %d, passo %d\n", rank, count);
    for(int i=0; i<rows; i++){
        for(int j=0; j<M; j++)
            printf(" %d ", forest[i][j]);
        printf("\n");
    }
}

void printForestFile(int **forest, int count, FILE *file){
    fprintf(file, "processo %d, passo %d\n", rank, count);
        for(int i=0; i<rows; i++){
            for(int j=0; j<M; j++)
                fprintf(file, " %d ", forest[i][j]);
            fprintf(file, "\n");
        }
}


// creazione di 2 lista in modo tale da dividere i 2 stati quindi stato prima dell'esecuzione e quello dopo
int** fireExtention(int **forest){
    int** newForest;
    newForest = (int**) malloc(rows * sizeof(int*));
    for(int i=0; i<rows; i++)
        newForest[i]= (int*) malloc(M * sizeof(int)); 
    
    if(size==1){ //se la funzione è eseguita da un solo processo
        checkIntermediateRows(forest,newForest);
    }else{
        /* per ogni processo invio riga precedente e successiva asincrono e faccio il calcolo, alla fine faccio ricezione
            sincrona delle righe per proseguire il calcolo della prima e dell'ultima riga dell'array */
        if(rank==0){
            int *rowDown = (int*) malloc(M*sizeof(int));
            MPI_Request request1;
            int *sendRow = forest[rows-1];
            MPI_Isend(sendRow, M, MPI_INT, rank+1, 0, MPI_COMM_WORLD, &request1);
            
            checkIntermediateRows(forest,newForest);

            MPI_Status status;
            MPI_Recv(rowDown, M, MPI_INT, rank+1, 1, MPI_COMM_WORLD, &status);

            //controllo solo l'ultima riga con con la riga successiva
            for(int i=0; i<M; i++)
                if(forest[rows-1][i]==1 && rowDown[i]==2)
                    newForest[rows-1][i]=2;

            free(rowDown);
        }else if(rank == size-1){
            
            int *rowUp = (int*) malloc(M*sizeof(int));
            MPI_Request request1;
            int *sendRow = forest[0];
            MPI_Isend(sendRow, M, MPI_INT, rank-1, 1, MPI_COMM_WORLD, &request1);

            checkIntermediateRows(forest,newForest);

            MPI_Status status;
            MPI_Recv(rowUp, M, MPI_INT, rank-1, 0, MPI_COMM_WORLD, &status);
    
            //controllo la prima riga con con la riga precedente
            for(int i=0; i<M; i++)
                if(forest[0][i]==1 && rowUp[i]==2)
                    newForest[0][i]=2;

            free(rowUp);
        }else{
            int *rowUp = (int*) malloc(M*sizeof(int));
            int *rowDown = (int*) malloc(M*sizeof(int));
            MPI_Request request1, request2;
            int *sendRow = forest[0];
            MPI_Isend(sendRow, M, MPI_INT, rank-1, 1, MPI_COMM_WORLD, &request1);
            sendRow = forest[rows-1];
            MPI_Isend(sendRow, M, MPI_INT, rank+1, 0, MPI_COMM_WORLD, &request2);

            checkIntermediateRows(forest,newForest);


            MPI_Status status1, status2;
            MPI_Recv(rowUp, M, MPI_INT, rank-1, 0, MPI_COMM_WORLD, &status1);
            MPI_Recv(rowDown, M, MPI_INT, rank+1, 1, MPI_COMM_WORLD, &status2);

            //controllo la prima riga con con la riga precedente
            for(int i=0; i<M; i++)
                if(forest[0][i]==1 && rowUp[i]==2)
                    newForest[0][i]=2;
            
            //controllo solo l'ultima riga con con la riga successiva
            for(int i=0; i<M; i++)
                if(forest[rows-1][i]==1 && rowDown[i]==2)
                    newForest[rows-1][i]=2;
            free(rowUp);
            free(rowDown);
        }
    }

    for(int i=0; i<rows; i++)
        free(forest[i]);
    free(forest);

    return newForest;
}

void checkIntermediateRows(int **forest, int **newForest){
    if(rows==1){ //se il processo ha una sola riga
        for(int i=0;i<M;i++){
            if(forest[0][i]==1){
                if(i==0){
                    if(forest[0][i+1]==2)
                        newForest[0][i]=2;
                    else
                        newForest[0][i]=forest[0][i];
                }else if(i==M-1){
                    if(forest[0][i-1]==2)
                        newForest[0][i]=2;
                    else
                        newForest[0][i]=forest[0][i]; 
                }else{
                    if(forest[0][i-1]==2)
                        newForest[0][i]=2;
                    else if(forest[0][i+1]==2)
                        newForest[0][i]=2;
                    else    
                        newForest[0][i]=forest[0][i];
                } 
            }else if(forest[0][i]==2){
                newForest[0][i]=0;
            }else{
                newForest[0][i]=forest[0][i];
            }
        }
    }else{
        for(int i=0; i<rows; i++)
            for(int j=0; j<M; j++){
                if(forest[i][j]==1){
                    if(i==0){
                        if(j==0){ //case 1 
                            if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else
                                newForest[i][j]=forest[i][j];
                        }else if(j==M-1){ //case 3
                            if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else    
                                newForest[i][j]=forest[i][j];
                        }else{ //case 2
                            if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else    
                                newForest[i][j]=forest[i][j];
                        }
                    }else if(i==rows-1){
                        if(j==0){ //case 4
                            if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else
                                newForest[i][j]=forest[i][j];
                        }else if(j==M-1){ //case 6
                            if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else
                                newForest[i][j]=forest[i][j];
                        }else{ //case 5
                            if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else    
                                newForest[i][j]=forest[i][j];
                        }
                    }else{
                        if(j==0){ //case 7
                            if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else    
                                newForest[i][j]=forest[i][j];
                        }else if(j==M-1){ //case 8
                            if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else    
                                newForest[i][j]=forest[i][j];
                        }else{ //case 9
                            if(forest[i-1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j+1]==2)
                                newForest[i][j]=2;
                            else if(forest[i+1][j]==2)
                                newForest[i][j]=2;
                            else if(forest[i][j-1]==2)
                                newForest[i][j]=2;
                            else{    
                                newForest[i][j]=forest[i][j];
                            }
                        }
                    }
                }else if(forest[i][j]==2){
                    newForest[i][j]=0;
                }else{
                    newForest[i][j]=forest[i][j];
                }
            }
    }
    return;
}

int isFinishedSequential(int **forest){
    for(int i=0; i<N; i++)
        for(int j=0; j<M; j++)
            if(forest[i][j]==2)
                return 0;
            
    return 1;
}

void extractFromFile(FILE *file, int iterazione){
    char stringa[21];
    int passo;
    fseek(file, 0, SEEK_SET);
    while(!feof(file)){
        fscanf(file, "%s", stringa);
        if(strcmp("passo", stringa)==0){
            fscanf(file, "%d", &passo);  
            if(passo==iterazione)
                return;
        }
    }
    printf("errore iterazione non trovata\n");
    exit(1);
}

int** fireExtentionSequential(int **forest){
    for(int i=0;i<N; i++)
        for(int j=0; j<M; j++)
            if(forest[i][j]==2)
                forest[i][j]=3;
    int **newForest;
    newForest = (int **) malloc(N * sizeof(int *));
    for(int i=0; i<N; i++)
        newForest[i] = (int *) malloc(M*sizeof(int));

    for(int i=0; i<N; i++)
        for(int j=0;j<M;j++){
            if(forest[i][j]==1){
                if(i==0){
                    if(j==0){  // caso 1 
                        if(forest[i][j+1]==3)
                            newForest[i][j]=2;
                        else if(forest[i+1][j]==3)
                            newForest[i][j]=2;
                        else    
                            newForest[i][j]= forest[i][j];
                    }else if(j==M-1){ //caso 3
                        if(forest[i][j-1]==3)
                            newForest[i][j]=2;
                        else if(forest[i+1][j]==3)
                            newForest[i][j]=2;
                        else    
                            newForest[i][j]= forest[i][j];
                    }else{ //caso 2
                        if(forest[i][j+1]==3)
                            newForest[i][j]=2;
                        else if(forest[i+1][j]==3)
                            newForest[i][j]=2;
                        else if(forest[i][j-1]==3)
                            newForest[i][j]=2;
                        else    
                            newForest[i][j]= forest[i][j];
                    }
                }else if(i==N-1){
                    if(j==0){  //caso 4
                        if(forest[i][j+1]==3)
                            newForest[i][j]=2;
                        else if(forest[i-1][j]==3)
                            newForest[i][j]=2;
                        else    
                            newForest[i][j]= forest[i][j];
                    }else if(j==M-1){ //caso 6
                        if(forest[i][j-1]==3)
                            newForest[i][j]=2;
                        else if(forest[i-1][j]==3)
                            newForest[i][j]=2;
                        else
                            newForest[i][j]=forest[i][j];
                    }else{ //caso 5
                        if(forest[i][j+1]==3)
                            newForest[i][j]=2;
                        else if(forest[i-1][j]==3)
                            newForest[i][j]=2;
                        else if(forest[i][j-1]==3)
                            newForest[i][j]=2;
                        else
                            newForest[i][j]=forest[i][j];
                    }
                }else if(j==0){  //caso 7
                    if(forest[i-1][j]==3)
                        newForest[i][j]=2;
                    else if(forest[i][j+1]==3)
                        newForest[i][j]=2;
                    else if(forest[i+1][j]==3)
                        newForest[i][j]=2;
                    else    
                        newForest[i][j]= forest[i][j];
                }else if(j==M-1){ //caso 8
                    if(forest[i-1][j]==3)
                        newForest[i][j]=2;
                    else if(forest[i+1][j]==3)
                        newForest[i][j]=2;
                    else if(forest[i][j-1]==3)
                        newForest[i][j]=2;
                    else
                        newForest[i][j]=forest[i][j];
                }else{ //caso 9
                    if(forest[i-1][j]==3)
                        newForest[i][j]=2;
                    else if(forest[i][j+1]==3)
                        newForest[i][j]=2;
                    else if(forest[i+1][j]==3)
                        newForest[i][j]=2;
                    else if(forest[i][j-1]==3)
                        newForest[i][j]=2;
                    else
                        newForest[i][j]=forest[i][j];
                    
                }
            }
            else
                newForest[i][j]=forest[i][j];
        }
    
    for(int i=0;i<N; i++)
        for(int j=0; j<M; j++)
            if(newForest[i][j]==3)
                newForest[i][j]=0;

    for(int i=0; i<N;i++)
        free(forest[i]);
    free(forest);
    return newForest;
}

int isEqual(int **forest, int pForest[][M]){
    for(int i=0;i<N;i++)
        for(int j=0;j<M;j++){
            if(forest[i][j]!=pForest[i][j]){
                printf("errore: i %d j %d, forest %d, pForest %d\n",i,j,forest[i][j], pForest[i][j]);
                return 0;
            }
        }
    return 1;
}

void correctness(){
    int **forest;
    forest = (int**) malloc(sizeof(int*)* N);
    for(int i=0;i<N;i++)
        forest[i] = (int*) malloc(sizeof(int) * M);
    int pForest[N][M];

    FILE *file[size];
    char nome[36];
    for(int i=0;i<size;i++){
        sprintf(nome, "./correctness/correctness%d.txt", i);
        file[i]=fopen(nome,"r");
        if(file==NULL){
            printf("errore apertura file %s\n", nome);
            exit(1);
        }
    }

    for(int i=0, n=0; i<size; i++){
        int rest = N%size; 
        int rows=1;
        if(rest > i)
            rows = (N/size) +1;  
        else    
            rows = N/size; 

        extractFromFile(file[i], 0); 
        for(int nLimit= n+rows;n<nLimit; n++)
            for(int m=0;m<M;m++)
                fscanf(file[i], "%d", &forest[n][m]);
    }
    printf("stampo foresta 0:\n" );
    for(int i=0;i<N;i++){
        for(int j=0;j<M; j++)
            printf(" %d ", forest[i][j]);
    printf("\n");
    }

    for(int x=1; isFinishedSequential(forest)==0; x++){
        forest = fireExtentionSequential(forest);
        for(int i=0, n=0; i<size; i++){
            int rest = N%size; 
            int rows=1;
            if(rest > i)
                rows = (N/size) +1;  
            else    
                rows = N/size; 

            extractFromFile(file[i], x); 
            for(int nLimit= n+rows;n<nLimit; n++)
                for(int m=0;m<M;m++)
                    fscanf(file[i], "%d", &pForest[n][m]);
        }
        printf("stampo foresta %d:\n", x );
        for(int i=0;i<N;i++){
            for(int j=0;j<M; j++)
                printf(" %d ", forest[i][j]);
        printf("\n");
        }
        printf("stampo pForesta %d:\n", x );
        for(int i=0;i<N;i++){
            for(int j=0;j<M; j++)
                printf(" %d ", pForest[i][j]);
        printf("\n");
        }

        if(!isEqual(forest, pForest)){
            printf("risultati contrastanti\n");
            exit(1);
        }
        
    }

    for(int i=0;i<size;i++)
        fclose(file[i]);
    for(int i=0;i<N;i++)
        free(forest[i]);
    free(forest);

    printf("Programma corretto\n");
}
