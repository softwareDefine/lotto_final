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
int filterLine = 0; // 필터 라인 수 세기 위한 변수
vector<Filter> filters; // 필터 저장 공간, csv파일에서 읽어서 여기에 저장할 예정

// 로또 번호 클래스 정의
class LottoNumber{
    public:
        int numbers[10];
        LottoNumber() {
            fill(begin(numbers), end(numbers), 0);
        }
};
vector<LottoNumber> pastNumbers; // 역대 당첨 번호 저장 공간, csv파일에서 읽어서 여기에 저장할 예정

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
                                }
                            } else if (columnIndex == 47) {
                                filter.minVal = cell[0] - '0';
                                filter.maxVal = cell[2] - '0';
                                if(filter.minVal < 0 || filter.maxVal > 6 || filter.minVal > filter.maxVal){
                                    throw invalid_argument("잘못된 범위입니다.");
                                }
                            } else{
                                continue;
                            }
                        }catch(exception& e){
                            // 필터 로그 큐에 결과 남기기
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

    for(auto &filter : filters){
        int matchCount[7] = {0, }; // 0~6개 일치하는 횟수 세기 위한 배열
        for(auto &lotto : pastNumbers){
            int count = 0;
            for(int i = 0; i < 6; i++){
                for(int j = 0; j < 6; j++){
                    if(filter.numbersWeight[lotto.numbers[j]] > 0 && lotto.numbers[j] == lotto.numbers[i]){
                        count++;
                        break;
                    }
                }
            }
            if(count >= filter.minVal && count <= filter.maxVal){
                matchCount[count]++;
            }
        }
        filter.filterBefore = pastNumbers.size();
        filter.errorLog = "필터 '" + filter.filterName + "' 적용 결과:\n";
        filter.matchLog = "";
        for(int i = 0; i < 6; i++){
            filter.errorLog += "번호 " + to_string(i) + ": " + to_string(filter.numbersWeight[i]) + "회\n";
        }
    }

    timer.endTime = timer.dateFormat();
    
    long long timeDiff = timer.parseDateTime(timer.endTime) - timer.parseDateTime(timer.startTime);
    cout << "시작 시간 : " << timer.startTime << endl;
    cout << "종료 시간 : " << timer.endTime << endl;
    cout << "총 가동 시간 : " << timeDiff/3600 << "시간 " << timeDiff%3600/60 << "분 " << timeDiff%60 << "초" << endl;

    cout << "필터링 : " << filterLine << " / " << filters.size() << " / " << filterLine-filters.size() << " / " << endl;
    cout << "데이터 : " << endl;
    return 0;
}