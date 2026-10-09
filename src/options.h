#pragma once

#include <iosfwd>
#include <string>
#include <vector>

// 실험용 명령행 옵션 모음. main에서 만들어 필요한 곳에 넘긴다 (전역 변수 없음)
struct Options {
    bool swapRB = false;          // --swap-rb: Bayer 패턴을 R↔B 바꿔 읽은 것처럼 처리 (실험 1-a)
    bool swapRBDemosaic = false;  // --swap-rb-demosaic: Demosaic에만 R↔B 바꾼 패턴 (실험 1-b)
    bool noBlack = false;         // --no-black: Black Level을 빼지 않음 (실험 2)
    bool wbNoclip = false;        // --wb-noclip: 자르지 않은 WBImg를 Demosaic에 넣음 (실험 3)
    bool quantizeLinear = false;  // --quantize-linear: Demosaic 결과를 8비트로 양자화한 뒤 감마 (밴딩 실험)

    // 검증 옵션: 실험이 아니라 평소 결과를 LibRaw 기준 이미지와 비교한다 (실험 옵션과 같이 쓸 수 없음)
    bool compareLibraw = false;   // --compare-libraw

    // 메타데이터만 기록하는 실행: open_file만 하고 unpack·ISP는 하지 않는다 (--csv 필요)
    bool metaOnly = false;        // --meta-only

    // 결과를 한 줄로 추가 기록할 csv 경로 (비어 있으면 기록 안 함)
    std::string csvPath;          // --csv <경로>

    // true면 png를 저장하지 않는다 (전수 실행용). 숫자만 csv에 남긴다
    bool noSave = false;          // --no-save

    // 실험 옵션이 하나라도 켜져 있는지 (켜져 있으면 exp_ 폴더에 저장)
    bool anyExperiment() const {
        return swapRB || swapRBDemosaic || noBlack || wbNoclip || quantizeLinear;
    }
};

enum class ParseResult { Ok, Help, Error };

// argv를 직접 파싱. "--"로 시작하지 않는 인자는 입력 파일(하나만)로 본다.
// Error일 때는 원인을 std::cerr에 출력한 뒤 돌아온다 (사용법은 호출한 쪽이 출력)
ParseResult ParseOptions(int argc, char* argv[], Options& opt, std::string& input);

void PrintUsage(std::ostream& os, const char* prog);

// 켜진 실험 옵션 이름 ("--" 없이), 고정된 순서: swap-rb, swap-rb-demosaic, no-black, wb-noclip, quantize-linear
std::vector<std::string> ExperimentOptions(const Options& opt);

// 켜진 옵션 전체 (실험 옵션 + compare-libraw). 시작할 때 목록 출력용
std::vector<std::string> EnabledOptions(const Options& opt);

// 저장 폴더 이름 (실험 옵션만 반영). 예: exp_no-black_wb-noclip
std::string ExpDirName(const Options& opt);
