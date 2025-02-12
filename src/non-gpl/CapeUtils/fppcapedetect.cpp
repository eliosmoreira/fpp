/*
 * This file is part of the Falcon Player (FPP) and is Copyright (C)
 * 2013-2022 by the Falcon Player Developers.
 *
 * The Falcon Player (FPP) is free software, and is covered under
 * multiple Open Source licenses.  Please see the included 'LICENSES'
 * file for descriptions of what files are covered by each license.
 *
 * This source file is covered under the CC-BY-ND as described in the
 * included LICENSE.CC-BY-ND file.  This file may be modified for
 * personal use, but modified copies MAY NOT be redistributed in any form.
 */

#include "CapeUtils.h"
#include <filesystem>
#include <stdlib.h>

int main(int argc, char* argv[]) {
    for (const auto& entry : std::filesystem::directory_iterator("/home/fpp/media/tmp/")) {
        std::filesystem::remove_all(entry.path());
    }

    if (CapeUtils::INSTANCE.initCape(false) == CapeUtils::CapeStatus::NOT_PRESENT) {
        exit(-1);
    }
}
