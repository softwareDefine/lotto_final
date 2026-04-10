/* 입출력 헤더 */
#include <iostream> // 콘솔
#include <fstream> // 파일
#include <sstream> // 문자열 스트림

/* 시간 관리 헤더 */
#include <ctime> // 시간 측정
#include <iomanip> // 날짜 파싱

// 문자열 관리
#include <string>

/* 저장 공간 관리 */
#include <vector>
#include <queue> // 필터 큐로 쌓아두고 처리하기 

/* 스레딩 헤더 */
#include <thread>
#include <mutex>
#include <condition_variable>
#include <chrono>

using namespace std;

/* 
- 아키텍처
파일 읽기 -> filter.csv, 역대당첨번호 메모리에 넣기 -> data.csv 큐에 넣기 -> 큐에다 빼고 필터링 -> 결과 저장 -> 결과 출력
*/

// 시간 관련 클래스 정의
class Timer{
    public:
        string startTime;
        string endTime;

    string dateFormat(){
        time_t now = time(nullptr);
        tm* local = localtime(&now);

        ostringstream oss;
        oss << put_time(local, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }

    time_t parseDateTime(const string& datetime) {
        tm t = {};
        istringstream ss(datetime);
        ss >> get_time(&t, "%Y-%m-%d %H:%M:%S");
        return mktime(&t);
    }
};
Timer timer;

// 필터 클래스 정의
class Filter{
public:
    string filterName, filterSummary;
    int numbersWeight[50];
    int minVal;
    int maxVal;
    long long filterBefore; // 필터링 전 후보 번호 수
    long long filterAfter; // 필터링 후 후보 번호 수
    string errorLog;
    string matchLog;
    Filter() : filterName(""), filterSummary(""), minVal(0), maxVal(0), filterBefore(0), filterAfter(0) {
        fill(begin(numbersWeight), end(numbersWeight), 0);
    }
};
long long filterLine = 0; // 필터 라인 수 세기 위한 변수
long long totalDataLines = 0; // 전체 데이터 라인 수 세기 위한 변수
long long errorDataLines = 0; // 데이터 오류 라인 수 세기 위한 변수
vector<Filter> filters; // 필터 저장 공간, csv파일에서 읽어서 여기에 저장할 예정

vector<thread> threads; // 스레드 저장 공간
mutex mtx; // 스레드 동기화 위한 뮤텍스
condition_variable cv; // 스레드 동기화 위한 조건 변수
bool finished = false; // 스레드 작업 완료 여부 체크 위한 변수

// 로또 번호 클래스 정의
class LottoNumber{
    public:
        int numbers[10];
        LottoNumber() {
            fill(begin(numbers), end(numbers), 0);
        }
};
class batchLottoNumbers{
    public:
        int nowFilterIndex; // 현재 필터링 중인 필터 인덱스
        vector<LottoNumber> lottoNumbers;
};
vector<LottoNumber> pastNumbers; // 역대 당첨 번호 저장 공간, csv파일에서 읽어서 여기에 저장할 예정
queue<batchLottoNumbers> candidateNumbers; // 후보 번호 저장 공간, data.csv에서 읽어서 여기에 저장할 예정

void readFilter(ifstream& file) {
    string line;
    bool inQuotes = false;
    bool isFirstRow = true;
    int columnNum = 1;
    while(getline(file, line)){
        // 첫 번째 행은 헤더이므로 건너뜁니다.
        if (isFirstRow) { isFirstRow = false; continue; }
        string cell;
        Filter filter;
        int columnIndex = 0;

        for(int i = 0; i < line.size(); i++){
            if(line[i] == '"') inQuotes = !inQuotes;
            else if(line[i] == ',' && !inQuotes){
                if (cell.front() == '"' && cell.back() == '"') {
                    cell = cell.substr(1, cell.size() - 2); // 양쪽 따옴표 제거
                }
                if(columnIndex > 47) break; // 열 개수 초과 시 처리 중단
                switch (columnIndex) {
                    case 0: filter.filterName = cell; break;
                    case 1: filter.filterSummary = cell; break;
                    default:
                        try{
                            if (columnIndex >= 2 && columnIndex < 47) {
                                int number = stoi(cell);
                                if(number > 0 && number <= 45){
                                    filter.numbersWeight[number]++;
                                }else if(number > 45){
                                    throw invalid_argument("45보다 큰 번호는 존재할 수 없습니다.");
                                }
                            } else if (columnIndex == 47) {
                                filter.minVal = cell[0] - '0';
                                filter.maxVal = cell[2] - '0';
                                if(filter.minVal < 0 || filter.maxVal > 6 || filter.minVal > filter.maxVal){
                                    throw invalid_argument("minVal은 0~6 사이, maxVal은 0~6 사이여야 하며, minVal이 maxVal보다 작아야 합니다.");
                                }
                            } else{
                                continue;
                            }
                        }catch(exception& e){
                            // 필터 로그 큐에 결과 남기기
                            filter.errorLog += string(e.what()) + " 열: " + to_string(columnIndex) + " 값: " + cell + ", ";
                            if (columnIndex >= 2 && columnIndex < 47) {
                            } else if (columnIndex == 47) {
                                filter.minVal = 0; filter.maxVal = 0;
                            }
                        }
                        break;
                }
                cell.clear();
                columnIndex++;
            } else {
                cell += line[i];
            }
        }
        // 마지막 셀 처리
        if (!cell.empty()) {
            if (columnIndex == 47) {
                filter.minVal = cell[0] - '0';
                filter.maxVal = cell[2] - '0';
                if(filter.minVal < 0 || filter.maxVal > 6 || filter.minVal > filter.maxVal){
                    filter.minVal = 0; filter.maxVal = 0;
                }
            }
        }
        filterLine++;
        filters.push_back(filter);
    }
}

void readPastNumbers(ifstream& file){
    string line;
    bool isFirstRow = true;
    while(getline(file, line)){
        if (isFirstRow) { isFirstRow = false; continue; }
        string cell;
        LottoNumber lottoNumber;
        int columnIndex = 0;

        for(int i = 0; i < line.size(); i++){
            if(line[i] == ','){
                try{
                    if(columnIndex < 7 && columnIndex >= 1){
                        int number = stoi(cell);
                        if(number > 0 && number <= 45){
                            lottoNumber.numbers[columnIndex-1] = number;
                        } else{
                            throw invalid_argument("잘못된 번호입니다.");
                        }
                    }
                }catch(exception& e){
                    lottoNumber.numbers[columnIndex-1] = 0; // 잘못된 번호는 0으로 처리
                }
                cell.clear();
                columnIndex++;
            } else {
                cell += line[i];
            }
        }
        // 마지막 셀 처리
        if (!cell.empty() && columnIndex < 7) {
            try{
                int number = stoi(cell);
                if(number > 0 && number <= 45){
                    lottoNumber.numbers[columnIndex-1] = number;
                } else{
                    throw invalid_argument("잘못된 번호입니다.");
                }
            }catch(exception& e){
                lottoNumber.numbers[columnIndex-1] = 0; // 잘못된 번호는 0으로 처리
            }
        }
        pastNumbers.push_back(lottoNumber);
    }
}

void readData(ifstream& file){
    string line;
    bool isFirstRow = true;
    batchLottoNumbers batchNumbers;
    while(getline(file, line)){
        if (isFirstRow) { isFirstRow = false; continue; }
        // data.csv 파일에서 필요한 데이터 읽어서 처리하는 로직 작성
        int redundant[50] = {0, }; // 1~45 번호 사용 여부 체크 위한 배열
        int columnIndex = 0;
        totalDataLines++; // 전체 데이터 라인 수 세기 위한 변수
        string cell = "";
        int flag = 0; // 데이터 오류 여부 체크 위한 변수
        LottoNumber lottoNumber;
        for(int i = 0; i < line.size(); i++){
            if(line[i] == ','){
                try{
                    int number = stoi(cell);
                    if(number < 0 || number > 45) {
                        throw invalid_argument("잘못된 번호입니다.");
                    }
                    if(redundant[number] > 0){
                        throw invalid_argument("중복된 번호입니다.");
                    }
                    redundant[number]++;
                    lottoNumber.numbers[columnIndex] = number;
                }catch(exception& e){
                    // 잘못된 데이터 처리
                    flag = 1;
                    break;
                }
                // 셀 데이터 처리, 예시로 후보 번호 생성
                cell ="";
                // 셀 구분자 처리
                columnIndex++;
            } else {
                cell += line[i];
                // 셀 데이터 처리
            }
        }
        if(cell != ""){ // 마지막 셀 처리
            try{
                int number = stoi(cell);
                if(number < 0 || number > 45) {
                    throw invalid_argument("잘못된 번호입니다.");
                }
                if(redundant[number] > 0){
                    throw invalid_argument("중복된 번호입니다.");
                }
                redundant[number]++;
                lottoNumber.numbers[columnIndex] = number;
            }catch(exception& e){
                // 잘못된 데이터 처리
                flag = 1;
                
            }
        }
        if(flag == 1){
            errorDataLines++; // 데이터 오류 라인 수 세기 위한 변수
            continue; // 데이터 오류 시 해당 라인 건너뛰기
        }
        batchNumbers.lottoNumbers.push_back(lottoNumber);
        if(totalDataLines % 5000 == 0){
            candidateNumbers.push(batchNumbers);
            batchNumbers.lottoNumbers.clear();
        }
        // 예시: 후보 번호 생성, 필터링 적용 등
    }
    // 남은 데이터 처리
    if(!batchNumbers.lottoNumbers.empty()){
        candidateNumbers.push(batchNumbers);
    }
}

void worker(int threadId){
    while (true) {
        batchLottoNumbers batchNumbers;
        {
            unique_lock<mutex> lock(mtx);

            // 큐에 작업이 들어올 때까지 기다림
            cv.wait(lock, [] {
                return !candidateNumbers.empty();
            });

            // 작업도 없고 끝났으면 종료
            if (candidateNumbers.empty() && finished) {
                return;
            }

            // 큐에서 작업 하나 꺼냄
            batchNumbers = candidateNumbers.front();
            candidateNumbers.pop();
        } // 여기서 lock 해제

        // lock 해제 후 실제 작업 수행
        for(auto &lottoNumber : batchNumbers.lottoNumbers){
            
        }
        // 예시: 필터링 작업 수행
        if(batchNumbers.nowFilterIndex < filters.size()){
            // 필터링 로직 작성
            batchNumbers.nowFilterIndex++;
            {
            unique_lock<mutex> lock(mtx);
            // 필터링 후 queue에 결과 저장하는 로직 작성
            candidateNumbers.push(batchNumbers);
            }
        }
    }
}

int main(){
    // 로또 프로그램 배너 출력
    cout << "=============================" << endl;
    cout << "Lotto Filtering Program v1.5" << endl;
    cout << "=============================" << endl << endl;
    timer.startTime = timer.dateFormat();

    // readOnly 모드로 csv파일 열기
    ifstream filterFile("filters.csv"); // csv파일은 같은 폴더 안에 있어야함 
    // filters 파일 안열리는 경우
    if (!filterFile.is_open()) {
        cerr << "필터파일을 열 수 없습니다. \n 필터파일이 같은 경로에 있는지 확인하고 csv파일이 맞는지 다시한번 확인하세요.";
        return 1;
    }
    readFilter(filterFile);
    filterFile.close();

    // readOnly 모드로 csv파일 열기
    ifstream pastNumbersFile("역대당첨번호.csv"); // csv파일은 같은 폴더 안에 있어야함 
    // pastNumbers 파일 안열리는 경우
    if (!pastNumbersFile.is_open()) {
        cerr << "역대당첨번호 파일을 열 수 없습니다.\n 필터파일이 같은 경로에 있는지 확인하고 csv파일이 맞는지 다시한번 확인하세요.";
        return 1;
    }
    readPastNumbers(pastNumbersFile);
    pastNumbersFile.close();

    // 필터와 역대당첨번호 중복 검사 및 일치하는 번호 개수 세기
    for(auto &filter : filters){
        int matchCount[7] = {0, }; // 0~6개 일치하는 횟수 세기 위한 배열
        for(auto &lotto : pastNumbers){
            int cnt = 0;
            for(int i = 0; i < 6; i++){
                if(filter.numbersWeight[lotto.numbers[i]] > 0){
                    cnt++;
                }
            }
            matchCount[cnt]++;
        }
        filter.filterBefore = pastNumbers.size();
        filter.matchLog = "";
        for(int i = 0; i < 7; i++){
            filter.matchLog += "당첨/" + to_string(i) + "=" + to_string(matchCount[i]) + (i == 6 ? "회" : "회, ");
        }
    }

    ifstream dataFile("data.csv"); // csv파일은 같은 폴더 안에 있어야함
    // data 파일 안열리는 경우
    if (!dataFile.is_open()) {
        cerr << "데이터 파일을 열 수 없습니다.\n 필터파일이 같은 경로에 있는지 확인하고 csv파일이 맞는지 다시한번 확인하세요.";
        return 1;
    }
    // data.csv batch로 읽어서 candidateNumbers에 저장하는 함수, batch로 읽는 이유는 메모리 효율성 때문, 5000줄씩 끊어서 batch로 저장
    readData(dataFile);
    dataFile.close();

    // 쓰레드 생성 및 필터링 작업 수행
    for(int i = 0; i < 3; i++){
        threads.emplace_back(worker, i+1);
    }

    // 더 이상 작업이 없음을 알림
    cv.notify_all(); // 모든 대기 중인 스레드 깨움
    // 스레드 종료 대기
    for(auto &t : threads){
        t.join();
    }

    timer.endTime = timer.dateFormat();
    
    long long timeDiff = timer.parseDateTime(timer.endTime) - timer.parseDateTime(timer.startTime);
    cout << "시작 시간 : " << timer.startTime << endl;
    cout << "종료 시간 : " << timer.endTime << endl;
    cout << "총 가동 시간 : " << timeDiff/3600 << "시간 " << timeDiff%3600/60 << "분 " << timeDiff%60 << "초" << endl;
    cout << endl;

    cout << "필터링 : " << filterLine << " / " << filters.size() << " / " << filterLine-filters.size() << " / " << filterLine/filters.size() * 100 << "%" << endl;
    cout << "데이터 : " << totalDataLines - errorDataLines << " / " << endl;
    cout << endl;

    
    return 0;
}