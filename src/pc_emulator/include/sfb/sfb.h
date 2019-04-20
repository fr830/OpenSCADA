#ifndef __PC_EMULATOR_INCLUDE_SFB_SFB_H
#define __PC_EMULATOR_INCLUDE_SFB_SFB_H
#include <iostream>
#include <fcntl.h>
#include <fstream>
#include <unordered_map>
#include <math.h>
#include "src/pc_emulator/proto/configuration.pb.h"
#include "src/pc_emulator/include/pc_configuration.h"
#include "src/pc_emulator/include/pc_resource.h"
#include "src/pc_emulator/include/pc_logger.h"

using namespace std;
using namespace pc_specification;

namespace pc_emulator {

    //! Generic abstract class for an SFB
    class SFB {
        public:

            PCResourceImpl * __AssociatedResource; /*!< Associated resource */
            string __SFBName;  /*!<  Name of the system function block */

            //! Called to execute the sfb
            /*!
                \param SFB     The SFB variable
            */
            virtual void Execute(PCVariable * SFB) = 0;
    };

}


#endif