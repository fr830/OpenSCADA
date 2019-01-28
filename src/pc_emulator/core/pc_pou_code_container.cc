#include <iostream>
#include <cstdint>
#include <cstring>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <vector>

#include "src/pc_emulator/include/pc_configuration.h"
#include "src/pc_emulator/include/pc_resource.h"
#include "src/pc_emulator/include/pc_pou_code_container.h"

using namespace std;
using namespace pc_emulator;

PoUCodeContainer& PoUCodeContainer::AddInstruction(string InsnString) {
    std::vector<string> results;
    boost::split(results, InsnString, [](char c){return c == ' ';});
    string InsnLabel;
    string InsnName;
    int StartIdx = 1;

    if (results.size() < 2) {
        __configuration->PCLogger->RaiseException("Incorrectly formatted "
            "instruction: " + InsnString);
    }

    if (boost::ends_with(results[0],":")) { //first element is label
        InsnLabel = results[0];
        InsnName = results[1];
    } else {
        InsnLabel = "";
        InsnName = results[0];
    }

    InsnContainer * container = new InsnContainer(InsnName, InsnLabel,
                                                __InsnCount);
    __InsnCount ++;

    if(!InsnLabel.empty()) {

        if(__InsnContainerByLabel.find(InsnLabel) 
                    != __InsnContainerByLabel.end()) {
            __configuration->PCLogger->RaiseException("Same label cannot be "
                    "assigned more than once!");
        }
        __InsnContainerByLabel.insert(std::make_pair(InsnLabel, 
                            container));
        StartIdx ++;
    }

    for(;StartIdx < results.size(); StartIdx ++) {
        container->AddOperand(results[StartIdx]);
    }

    __Insns.push_back(container);
    return *this;
}

void PoUCodeContainer::Cleanup() {
    for(auto it = __Insns.begin(); it != __Insns.end(); it ++) {
        delete *it;
    }

    __Insns.clear();
}

int PoUCodeContainer::GetTotalNumberInsns() {
    return __InsnCount;
}

InsnContainer * PoUCodeContainer::GetInsn(int pos) {
    if (pos < __InsnCount && pos >= 0) {
        return __Insns[pos];
    }
    return nullptr;
}

InsnContainer * PoUCodeContainer::GetInsnAT(string label) {
    auto got = __InsnContainerByLabel.find(label);
    if (got == __InsnContainerByLabel.end())
        return nullptr;
    return got->second;
}