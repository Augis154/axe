#ifndef GEN_QBE_H
#define GEN_QBE_H

#include "ir.h"

// Writes `module` as QBE IL to `filename`. Returns false if the file could not be written.
bool generate_qbe(const char *filename, const IrModule *module);

#endif // GEN_QBE_H
