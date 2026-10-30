#pragma once
#include <cstddef>
#include <string_view>
constexpr bool WorkerdProfileStepCountAdmitted(std::size_t count) { return count>=1 && count<=128; }
constexpr bool WorkerdProfilePathAdmitted(std::string_view path) {
    return !path.empty() && path.size()<=256 && path.front()=='/' &&
        path.find_first_not_of("/abcdefghijklmnopqrstuvwxyz0123456789-_")==path.npos;
}
int WorkerdProfile(int argc,char** argv);
int WorkerdSignalMaskProbe();
void WorkerdSpawnSelfTest(const char* executable);
