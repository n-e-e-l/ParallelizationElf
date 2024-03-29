#ifndef ELF_FINAL64_NODELEVEL_ENQUEUE_H
#define ELF_FINAL64_NODELEVEL_ENQUEUE_H

#include "Util.h"
#include "Utils/structures.h"


#include "SortingEngine/sortingEngine.h"
#include "ThreadPool.h"
#include <queue>
#include <functional>
#include <map>
#include <stdlib.h>
vector<std::future<void>> results_get;
std::mutex values_mutex;
//std::mutex values_mutex3;



class Elf_final64_nodelevel_enqueue : public Index {
protected:
    uint32_t* ELF;
    uint32_t* max_dims;
    const uint32_t NUM_DIM=0;
    uint32_t MAX_FIRST_DIM;
    uint32_t NUM_PREDICATES;
    const uint32_t INITIAL_BUCKET_SIZE = 16;
    int num_threads;
    ThreadPool *pool;


//    ThreadPool *pool = new ThreadPool(Elf_final64_nodelevel_enqueue::num_threads);



public:

    struct FrequencyItem {
        uint32_t count;
        uint32_t* tids;

    };
    typedef FrequencyItem FrequencyItem;
    typedef map<uint32_t, FrequencyItem*> FrequencyMap;


    uint32_t* getElf(){
        return this->ELF;
    }

    Elf_final64_nodelevel_enqueue(Store* s, uint32_t* max, uint32_t* ELF) : Index(s), max_dims(max), NUM_DIM(s->NUM_DIM) {
        //nothing todo
        this->ELF = ELF;
    }


    Elf_final64_nodelevel_enqueue(Store* s, uint32_t* max) : Index(s), max_dims(max), NUM_DIM(s->NUM_DIM) {
        //nothing todo
    }

    Elf_final64_nodelevel_enqueue(Store* s, Elf_final64_nodelevel_enqueue* elf) : Index(s), ELF(elf->ELF), NUM_DIM(elf->NUM_DIM), MAX_FIRST_DIM(elf->MAX_FIRST_DIM) {
        //nothing todo
    }

    ~Elf_final64_nodelevel_enqueue() {
        delete[] ELF;
    };

    void setNumThreads(int thread_num){
        this->num_threads = thread_num;
        this->pool = new ThreadPool(this->num_threads);
    }

    void join_threads(){
//        this->pool->shutdown();
        delete this->pool;
    }


    void setNumPredicates(uint32_t numColumns) {
        NUM_PREDICATES = numColumns;
    }

    inline uint64_t get64(uint32_t* ptr) {
        return *((uint64_t*) ptr);
    }

    inline void set64(uint32_t* ptr, uint64_t val) {
        /*  if(val != INVALID_POINTER && val > writePointer) {
              counter++;
          }*/
        *((uint64_t*) ptr) = val;
    }

    uint32_t exactMatch(uint32_t* query) {
        uint32_t compareValue;
        uint32_t dim;

        uint64_t pointer = get64(&(ELF[query[0]*2])); //exploit density property in first dim

        dim = 1; //start @dim 1
        while (pointer < LAST_ENTRY_MASK64) {//not found monolist begin assuming there are mono lists, XXX dirty hack NOT_FOUND is -1 (hopefully) interpreted as MAX_INT
            compareValue = query[dim++];
            pointer = listContains(compareValue, pointer);
        }
        if (pointer == NOT_FOUND)
            return NOT_FOUND;

        pointer &= RECOVER_MASK64;
        uint32_t value;
        for (; dim < NUM_DIM; dim++) {
            compareValue = query[dim];
            value = ELF[pointer++];
            if (compareValue != value)
                return NOT_FOUND;
        }

        return ELF[pointer] & RECOVER_MASK;
    }

    inline uint64_t listContains(uint32_t VALUE, uint64_t START) {
        uint32_t toCompare;
        uint64_t position = START;

        while ((toCompare = ELF[position]) < LAST_ENTRY_MASK) {//not found end of list, is skipped totally for 1-element lists
            if (VALUE == toCompare)
                return get64(&(ELF[position + 1]));
            else if (VALUE < toCompare)
                return NOT_FOUND;
            //else continue search
            position += 3;
        }

        toCompare &= RECOVER_MASK; //unset bit indicating we found end of list
        return (VALUE == toCompare) ? get64(&(ELF[position + 1])) : NOT_FOUND;
    }

    void buildIndex() {
        //uint32_t requiredInts = d->requiredInts();
        uint64_t requiredInts = ((uint64_t) STORE->NUM_POINTS)*(NUM_DIM + 3);
#ifndef NO_SIMD_OPERATIONS

        //cout << "Zahl "<< requiredInts<<endl;
        void* temp;
        if (posix_memalign(&temp, 16, 2 * requiredInts * sizeof (uint32_t)) != 0) {
            cout << "memalign failed!" << endl;
        }
        ELF = (uint32_t*) temp;
#else
        ELF = new uint32_t[2 * requiredInts];
#endif
        uint64_t memory = buildFromDataSorting(STORE->STORE, STORE->NUM_POINTS, STORE->NUM_DIM);

        cout << "Size: " << memory << " : " << ((uint64_t) NUM_DIM)*(STORE->NUM_POINTS) << endl;

    }

    struct DimList {
        uint64_t* begin;
        uint32_t* length;
    };

    uint64_t createDimListSorting(Array<uint32_t>* data_array, uint32_t numDim, uint32_t dim, uint64_t writePointer) {
        uint32_t freqListSize = 0;
        DimList dimlist;

        sort(data_array, dim, max_dims, numDim);


        uint64_t* nextListPositions = (uint64_t*) malloc(sizeof (uint64_t) * data_array->size);
        dimlist.begin = (uint64_t*) malloc(sizeof (uint64_t) * data_array->size);
        dimlist.length = (uint32_t*) malloc(sizeof (uint64_t) * data_array->size);
        uint32_t begin = 0;
        uint32_t cur = data_array->ary[0].value[dim];
        uint32_t i;
        for (i = 1; i < data_array->size; i++) {
            if (cur != data_array->ary[i].value[dim]) {
                ELF[writePointer] = cur;
                writePointer++;
                nextListPositions[freqListSize] = writePointer;
                writePointer += 2;

                dimlist.begin[freqListSize] = begin;
                dimlist.length[freqListSize] = i - begin;
                begin = i;
                cur = data_array->ary[i].value[dim];
                freqListSize++;
            }
        }
        ELF[writePointer] = cur;
        writePointer++;
        nextListPositions[freqListSize] = writePointer;
        writePointer += 2;
        ELF[writePointer - 3] |= LAST_ENTRY_MASK;

        dimlist.begin[freqListSize] = begin;
        dimlist.length[freqListSize] = i - begin;
        freqListSize++;


        nextListPositions = (uint64_t*) realloc(nextListPositions, sizeof (uint64_t) * freqListSize);
        dimlist.begin = (uint64_t*) realloc(dimlist.begin, sizeof (uint64_t) * freqListSize);
        dimlist.length = (uint32_t*) realloc(dimlist.length, sizeof (uint32_t) * freqListSize);

        Array<uint32_t> temp;
        if (dim + 1 < numDim) {
            for (int j = 0; j < freqListSize; j++) {
                temp.ary = &(data_array->ary[dimlist.begin[j]]);
                temp.size = dimlist.length[j];

                if (dimlist.length[j] > 1) {
                    set64(&(ELF[nextListPositions[j]]), writePointer);
                    writePointer = createDimListSorting(&temp, numDim, dim + 1, writePointer);
                } else {
                    set64(&(ELF[nextListPositions[j]]), writePointer | LAST_ENTRY_MASK64);
                    writePointer = linearize_elf_mono(writePointer, temp.ary[0].tid, dim + 1);
                    ELF[writePointer - 1] |= LAST_ENTRY_MASK;

                }

            }
        } else {
            for (int j = 0; j < freqListSize; j++) {
                for (int point = 0; point < dimlist.length[j]; point++) {
                    set64(&(ELF[nextListPositions[j]]), writePointer | LAST_ENTRY_MASK64);
                    writePointer = linearize_elf_mono(writePointer, data_array->ary[dimlist.begin[j] + point].tid, dim + 1);
                }
                ELF[writePointer - 1] |= LAST_ENTRY_MASK;
            }
        }
        //cout << ELF[5051] << endl;
        free(nextListPositions);
        free(dimlist.begin);
        free(dimlist.length);

        return writePointer;




    }

    uint64_t buildFromDataSorting(uint32_t** store, uint32_t numRows, uint32_t numDim) {

        //We don't know anything about the first deimension
        //   -> worst case, we have to store as many lists as points

        Array<uint32_t> data_array;
        data_array.ary = (TID<uint32_t>*) malloc(sizeof (TID<uint32_t>) * numRows);
        data_array.size = numRows;
        uint32_t* tempArray = new uint32_t[((size_t) numRows) * numDim];
        for (uint64_t i = 0; i < numRows; i++) {
            data_array.ary[i].tid = i;
            data_array.ary[i].value = &(tempArray[i * numDim]);
            memcpy(data_array.ary[i].value, store[i], sizeof (uint32_t) * numDim);
        }
        sort(&data_array, 0, &(max_dims[0]), numDim);

        uint64_t writepointer = 0;
        this->MAX_FIRST_DIM = data_array.ary[numRows - 1].value[0];
        writepointer += (MAX_FIRST_DIM + 1) * 2;
        uint32_t begin = 0;


        uint32_t cur = data_array.ary[0].value[0];
        uint32_t size;
        uint32_t pos = 0;
        uint32_t i;
        Array<uint32_t> temp;
        for (i = 1; i < numRows; i++) {
            if (cur != data_array.ary[i].value[0]) {
                size = i - begin;
                temp.ary = &(data_array.ary[begin]);
                temp.size = size;
                if (size > 1) {
                    set64(&(ELF[pos * 2]), writepointer);
                    writepointer = createDimListSorting(&temp, numDim, 1, writepointer);
                } else {
                    set64(&(ELF[pos * 2]), writepointer | LAST_ENTRY_MASK64);
                    writepointer = linearize_elf_mono(writepointer, temp.ary[0].tid, 1);

                }
                begin = i;
                cur = data_array.ary[i].value[0];
                pos++;
            }
        }
        size = i - begin;
        temp.ary = &(data_array.ary[begin]);
        temp.size = size;
        if (size > 1) {
            set64(&(ELF[pos * 2]), writepointer);
            writepointer = createDimListSorting(&temp, numDim, 1, writepointer);
        } else {
            set64(&(ELF[pos * 2]), writepointer | LAST_ENTRY_MASK64);
            writepointer = linearize_elf_mono(writepointer, temp.ary[begin].tid, 1);

        }


        delete[] tempArray;

        return writepointer;
    }

    uint64_t buildFromData() {

        //We don't know anything about the first deimension
        //   -> worst case, we have to store as many lists as points
        uint64_t writepointer = 0;
        FrequencyItem* freqList;
        uint32_t num_FreqLists = createFirstFrequencyList(&freqList, STORE->NUM_POINTS, 0);
        this->MAX_FIRST_DIM = num_FreqLists - 1;
        writepointer += num_FreqLists * 2;

        for (uint32_t i = 0; i < num_FreqLists; i++) {

            if (freqList[i].count > 1) {
                set64(&(ELF[i * 2]), writepointer);
                writepointer = linearize_dim_list(writepointer, freqList[i].tids, freqList[i].count, 1);
            } else {
                set64(&(ELF[i * 2]), writepointer | LAST_ENTRY_MASK64);
                writepointer = linearize_elf_mono(writepointer, freqList[i].tids[0], 1);

            }
            free(freqList[i].tids);

        }
        free(freqList);



        return writepointer;
    }


    //can exploit density criteria

    uint32_t createFirstFrequencyList(FrequencyItem** freqList, uint32_t num_points, uint32_t dim) {
        *freqList = (FrequencyItem*) malloc(num_points * sizeof (FrequencyItem));
        if (!freqList) {
            cout << "malloc freqList failed!" << endl;
        }
        uint32_t freqListSize = 0;
        FrequencyItem* freqItem;
        for (uint32_t i = 0; i < num_points; i++) {
            freqItem = &((*freqList)[STORE->getPoint(i)[dim]]);
            if (!freqItem->tids) {
                freqItem->count = 0;
                freqItem->tids = (uint32_t*) malloc(INITIAL_BUCKET_SIZE * sizeof (uint32_t));
                if (!freqItem->tids) {
                    cout << "malloc failed!" << endl;
                }
                freqListSize++;
            }
            freqItem->tids[freqItem->count] = i;
            freqItem->count += 1;

            if (freqItem->count % 2 == 0) {
                freqItem->tids = (uint32_t*) realloc(freqItem->tids, freqItem->count * 2 * sizeof (uint32_t));
                if (!freqItem->tids) {
                    cout << "realloc failed!" << endl;
                }
            }

        }
        return freqListSize;
    }

    uint64_t linearize_dim_list(uint64_t writePointer, uint32_t* tids, uint32_t num_points, uint32_t dim) {
        //We don't know anything about the first deimension
        //   -> worst case, we have to store as many lists as points
        FrequencyMap* freqList = new FrequencyMap();
        uint32_t num_FreqLists = createFrequencyList(freqList, tids, num_points, dim);
        FrequencyMap::iterator iter;
        uint64_t* nextListPositions = new uint64_t[num_FreqLists];
        uint32_t i;
        for (iter = freqList->begin(), i = 0; iter != freqList->end(); iter++, i++) {
            ELF[writePointer] = iter->first;
            writePointer++;
            nextListPositions[i] = writePointer;
            writePointer += 2;
        }
        ELF[writePointer - 3] |= LAST_ENTRY_MASK;

        for (iter = freqList->begin(), i = 0; iter != freqList->end(); iter++, i++) {

            if (iter->second->count > 1) {
                set64(&(ELF[nextListPositions[i]]), writePointer);
                writePointer = linearize_dim_list(writePointer, iter->second->tids, iter->second->count, dim + 1);
            } else {
                set64(&(ELF[nextListPositions[i]]), writePointer | LAST_ENTRY_MASK64);
                writePointer = linearize_elf_mono(writePointer, iter->second->tids[0], dim + 1);

            }
            free(iter->second->tids);
            free(iter->second);

        }
        delete freqList;
        delete[] nextListPositions;

        return writePointer;
    }

    uint32_t createFrequencyList(FrequencyMap* freqList, uint32_t* tids, uint32_t num_points, uint32_t dim) {

        uint32_t* point;
        uint32_t freqListSize = 0;
        FrequencyItem* freqItem;
        FrequencyMap::iterator iter;
        for (uint32_t i = 0; i < num_points; i++) {
            point = STORE->getPoint(tids[i]);
            if ((iter = freqList->find(point[dim])) == freqList->end()) {
                freqItem = (FrequencyItem*) malloc(sizeof (FrequencyItem));
                if (!freqItem) {
                    cout << "FreqItem malloc failed!" << endl;
                }
                freqItem->count = 0;
                freqItem->tids = (uint32_t*) malloc(INITIAL_BUCKET_SIZE * sizeof (uint32_t));
                if (!freqItem->tids) {
                    cout << "malloc failed!" << endl;
                }
                iter = freqList->insert(pair<uint32_t, FrequencyItem*>(point[dim], freqItem)).first;
                freqListSize++;
            }
            iter->second->tids[ iter->second->count] = tids[i];
            iter->second->count += 1;

            if (iter->second->count % 2 == 0) {
                iter->second->tids = (uint32_t*) realloc(iter->second->tids, iter->second->count * 2 * sizeof (uint32_t));
                if (!iter->second->tids) {
                    cout << "realloc failed!" << endl;
                }
            }

        }
        return freqListSize;
    }

    uint64_t linearize_elf_mono(uint64_t writePointer, uint32_t tid, uint32_t DIMENSION) {
        uint32_t* point = STORE->getPoint(tid);
        memcpy(&(ELF[writePointer]), &(point[DIMENSION]), sizeof (uint32_t)*(NUM_DIM - DIMENSION));
        writePointer += (NUM_DIM - DIMENSION);
        ELF[writePointer++] = tid;

        return writePointer;
    }

    vector<uint32_t>* windowQuery(uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery) {
        vector<uint32_t>* resultTIDs = new vector<uint32_t>();
        uint32_t LOWER = lowerBoundQuery[FIRST_DIM]*2;
        uint32_t UPPER = upperBoundQuery[FIRST_DIM]*2;
        uint64_t pointerNextDim;

        for (uint32_t offset = LOWER; offset <= UPPER; offset += 2) {
            pointerNextDim = get64(&(ELF[offset]));
            if (pointerNextDim < LAST_ENTRY_MASK64) {

                evluateWindowOnList(1, pointerNextDim, resultTIDs, lowerBoundQuery, upperBoundQuery);
            } else {
                pointerNextDim &= RECOVER_MASK64; //Unique value in First dim -> MonList found
                evluateWindowOnMonoList(1, pointerNextDim, resultTIDs, lowerBoundQuery, upperBoundQuery);
            }
        }

        //        sort(resultTIDs->begin(), resultTIDs->end());
        return resultTIDs;
    }

    void evluateWindowOnList(uint32_t DIMENSION, uint64_t START_LIST, vector<uint32_t>* RESULTS, uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery) {
        uint32_t toCompare;
        uint64_t position = START_LIST;
        uint64_t pointer;

        while ((toCompare = ELF[position]) < LAST_ENTRY_MASK) {//not found end of list, is skipped totally for 1-element list
            if (isIn(lowerBoundQuery[DIMENSION], upperBoundQuery[DIMENSION], toCompare)) {
                pointer = get64(&(ELF[position + 1]));
                if (pointer < LAST_ENTRY_MASK64)//no monlist found
                    evluateWindowOnList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery);
                else {
                    pointer &= RECOVER_MASK64;
                    evluateWindowOnMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery);
                }
            } else if (upperBoundQuery[DIMENSION] < toCompare) {//XXX @David scheint sinn zu machen
                return;
            }//else continue

            position += 3;
        }

        //process last value
        toCompare &= RECOVER_MASK;
        if (isIn(lowerBoundQuery[DIMENSION], upperBoundQuery[DIMENSION], toCompare)) {
            pointer = get64(&(ELF[position + 1]));
            if (pointer < LAST_ENTRY_MASK64)//no monlist found
                evluateWindowOnList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery);
            else {
                pointer &= RECOVER_MASK64;
                evluateWindowOnMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery);
            }
        }
    }

    inline void evluateWindowOnMonoList(uint32_t DIMENSION, uint64_t START_MONO_LIST, vector<uint32_t>* RESULTS, uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery) {
        uint32_t value;
        uint32_t dim;
        uint32_t i;

        for (i = 0, dim = DIMENSION; dim < NUM_DIM; dim++, i++) {
            value = ELF[START_MONO_LIST + i];
            if (!isIn(lowerBoundQuery[dim], upperBoundQuery[dim], value))
                return;
        }

        RESULTS->push_back(ELF[START_MONO_LIST + i] & RECOVER_MASK);
    }

    vector<uint32_t>* partialMatch(uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery, bool* columnsForSelect) {
//        cout << "nodelevel_enque";


        vector<uint32_t> *resultTIDs = new vector<uint32_t>();
        uint32_t LOWER;
        uint32_t UPPER;
        std::vector<uint64_t> pointerNextDim;

        if (columnsForSelect[FIRST_DIM]) {
            LOWER = lowerBoundQuery[FIRST_DIM] * 2;
            UPPER = upperBoundQuery[FIRST_DIM] * 2;
        } else {
            LOWER = 0;
            UPPER = MAX_FIRST_DIM * 2;
        }



//        Implementation of separating first and other pointers
        for (uint32_t offset = LOWER; offset <= UPPER; offset += 2) {
            pointerNextDim.push_back(get64(&(ELF[offset])));
        }

//        int i = 0;
        for (auto it = std::next(pointerNextDim.begin()); it != pointerNextDim.end(); ++it) {
            if (*it < LAST_ENTRY_MASK64) {
                std::lock_guard<std::mutex> lk(values_mutex);

                results_get.push_back(pool->submit([=]{
                    this->partialMatch1(1, *it, resultTIDs, lowerBoundQuery, upperBoundQuery, columnsForSelect);

                }));

            } else {
                *it &= RECOVER_MASK64; //Unique value in First dim -> MonList found

//                std::unique_lock<std::mutex> lock(m_mutex);
                std::lock_guard<std::mutex> lk(values_mutex);

                results_get.push_back((pool->submit([=] {this->partialMatchMonoList(1, *it, resultTIDs, lowerBoundQuery, upperBoundQuery, columnsForSelect);})));

            }


        }


        if (pointerNextDim.front() < LAST_ENTRY_MASK64) {
            partialMatch1(1, pointerNextDim.front(), resultTIDs, lowerBoundQuery, upperBoundQuery, columnsForSelect);

        } else {
            uint64_t pointer = pointerNextDim.front();
            pointer &= RECOVER_MASK64;
            partialMatchMonoList(1, pointer, resultTIDs, lowerBoundQuery, upperBoundQuery, columnsForSelect);

        }
        pool->queue_empty();
        /*       while(true) {
       //
                   {

                       std::unique_lock<std::mutex> values_mutex2;
                       if(i+1 < results_get.size())
                           i++;
                       else{
                           values_mutex2.unlock();
                           break;
                       }


                   }
                       std::unique_lock<std::mutex> values_mutex2;
                   while (results_get[i].wait_for(std::chrono::seconds(10)) != future_status::ready && results_get[i].wait_for(std::chrono::seconds(10)) != future_status::deferred);
                   results_get[i].get();
                   values_mutex2.unlock();

               }*/
        for (int i=0; i < results_get.size();i++ ) {
            while (results_get[i].wait_for(std::chrono::seconds(1)) != future_status::ready && results_get[i].wait_for(std::chrono::seconds(1)) != future_status::deferred);
            try {
                if (results_get[i].valid())
                    results_get[i].get();
                else
                {
                    results_get[i].wait_for(std::chrono::seconds(1));
                    results_get[i].get();

                }
            } catch (const std::future_error& e) {
                std::cout << "Caught a future_error with code \"" << e.code()
                          << "\"\nMessage: \"" << e.what() << "\"\n";

            }

        }

        results_get.clear();

        return resultTIDs;

    }


    void partialMatch1(uint32_t DIMENSION, uint64_t START_LIST, vector<uint32_t>* RESULTS, uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery, bool* columnsForSelect) {
        uint32_t toCompare;
        uint64_t position = START_LIST;
        uint64_t pointer;
        uint64_t buffer = 0;

        if (columnsForSelect[DIMENSION]) {
            while ((toCompare = ELF[position]) < LAST_ENTRY_MASK) {//not found end of list, is skipped totally for 1-element list
                if (isIn(lowerBoundQuery[DIMENSION], upperBoundQuery[DIMENSION], toCompare)) {
                    pointer = get64(&(ELF[position + 1]));
                    if (buffer == 0){
                        buffer = pointer;
                        position = position + 3;
                        continue;
                    }
                    if (pointer < LAST_ENTRY_MASK64) {//no monlist found

                        std::lock_guard<std::mutex> lk(values_mutex);

                        results_get.push_back(pool->submit([=] {
                            this->partialMatch1(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery,
                                                columnsForSelect);}));


                    }
                    else {
                        pointer &= RECOVER_MASK64;

                        std::lock_guard<std::mutex> lk(values_mutex);

                        results_get.push_back(pool->submit([=] {
                            this->partialMatchMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery,
                                                       upperBoundQuery, columnsForSelect);
                        }));

                    }
                } else if (upperBoundQuery[DIMENSION] < toCompare) {//XXX @David scheint sinn zu machen
                    break;
                }//else continue

                position += 3;
            }

            //process last value
            toCompare &= RECOVER_MASK;
            if (isIn(lowerBoundQuery[DIMENSION], upperBoundQuery[DIMENSION], toCompare)) {
                pointer = get64(&(ELF[position + 1]));
                if (pointer < LAST_ENTRY_MASK64) {//no monlist found

                    std::lock_guard<std::mutex> lk(values_mutex);

                    results_get.push_back(pool->submit([=] {
                        this->partialMatch1(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery,
                                            columnsForSelect);
                    }));
                }
                else {
                    pointer &= RECOVER_MASK64;

                    std::lock_guard<std::mutex> lk(values_mutex);

                    results_get.push_back(pool->submit([=]{this->partialMatchMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery, columnsForSelect);}));

                }
            }
            if (buffer != 0) {
                if (buffer < LAST_ENTRY_MASK64)
                    partialMatch1(DIMENSION + 1, buffer, RESULTS, lowerBoundQuery, upperBoundQuery, columnsForSelect);
                else
                    partialMatchMonoList(DIMENSION + 1, buffer & RECOVER_MASK64, RESULTS, lowerBoundQuery,
                                         upperBoundQuery, columnsForSelect);
            }
        } else {
            while ((toCompare = ELF[position]) < LAST_ENTRY_MASK) {//not found end of list, is skipped totally for 1-element list
                pointer = get64(&(ELF[position + 1]));
                if (buffer == 0){
                    buffer = pointer;
                    position = position + 3;
                    continue;
                }
                if (pointer < LAST_ENTRY_MASK64) {//no monlist found

                    std::lock_guard<std::mutex> lk(values_mutex);

                    results_get.push_back(pool->submit([=] {
                        this->partialMatch1(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery,
                                            columnsForSelect);
                    }));
                }
                else {
                    pointer &= RECOVER_MASK64;

                    std::lock_guard<std::mutex> lk(values_mutex);

                    results_get.push_back(pool->submit([=]{this->partialMatchMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery, columnsForSelect);}));

                }
                position += 3;
            }

            //process last value
            toCompare &= RECOVER_MASK;
            pointer = get64(&(ELF[position + 1]));
            if (pointer < LAST_ENTRY_MASK64){//no monlist found

                std::lock_guard<std::mutex> lk(values_mutex);

                results_get.push_back(pool->submit([=] {this->partialMatch1(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery,columnsForSelect);}));
            }

            else {
                pointer &= RECOVER_MASK64;

                std::lock_guard<std::mutex> lk(values_mutex);

                results_get.push_back(pool->submit([=]{this->partialMatchMonoList(DIMENSION + 1, pointer, RESULTS, lowerBoundQuery, upperBoundQuery, columnsForSelect);}));
            }

            if (buffer != 0) {
                if (buffer < LAST_ENTRY_MASK64)
                    partialMatch1(DIMENSION + 1, buffer, RESULTS, lowerBoundQuery, upperBoundQuery, columnsForSelect);
                else
                    partialMatchMonoList(DIMENSION + 1, buffer & RECOVER_MASK64, RESULTS, lowerBoundQuery,
                                         upperBoundQuery, columnsForSelect);
            }
        }
    }

    void partialMatchMonoList(uint32_t DIMENSION, uint64_t START_MONO_LIST, vector<uint32_t>* RESULTS, uint32_t* lowerBoundQuery, uint32_t* upperBoundQuery, bool* columnsForSelect) {
        uint32_t value;
        uint32_t i = 0;
        std::vector<uint32_t > result_test;
        std::mutex mutex;

        for (; DIMENSION + i < NUM_DIM; i++) {
            if (columnsForSelect[DIMENSION + i]) {
                value = ELF[START_MONO_LIST + i];
                if (!isIn(lowerBoundQuery[DIMENSION + i], upperBoundQuery[DIMENSION + i], value))
                    return;
            }
        }
        result_test.push_back(ELF[START_MONO_LIST + (NUM_DIM - DIMENSION)] & RECOVER_MASK);

        std::lock_guard<std::mutex> lk(values_mutex);
        RESULTS->insert(RESULTS->end(), result_test.begin(), result_test.end());

        result_test.clear();

    }

};

#endif // ELF_FINAL_H
