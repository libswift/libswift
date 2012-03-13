/*
 * serialize.h
 *
 *  Created by Arno Bakker
 *  Copyright 2010-2012 TECHNISCHE UNIVERSITEIT DELFT. All rights reserved.
 *
 */

#ifndef SWIFT_SERIALIZE_H_
#define SWIFT_SERIALIZE_H_

#include <stdio.h>

#define fprintf_retiffail(...) { if (fprintf(__VA_ARGS__) < 0) { return -1; }}
#define fscanf_retiffail(...) { if (fscanf(__VA_ARGS__) == EOF) { return -1; }}

class Serializable {
  public:
	virtual int serialize(FILE *fp) = 0;
	virtual int deserialize(FILE *fp) = 0;
};

#endif /* SWIFT_SERIALIZE_H_ */
