/*
 * operational.h
 *
 *  Created on: Jun 22, 2012
 *      Author: arno
 */

#ifndef OPERATIONAL_H_
#define OPERATIONAL_H_

namespace swift
{

class Operational
{
   public:
	  Operational(bool working=true) { working_ = working; }
	  bool IsOperational() { return working_; }
	  void SetBroken() { working_ = false; }
   protected:
	  bool	working_;
};


};
#endif /* OPERATIONAL_H_ */
