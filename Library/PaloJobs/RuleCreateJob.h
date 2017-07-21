/* 
 *
 * Copyright (C) 2006-2014 Jedox AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (Version 2) as published
 * by the Free Software Foundation at http://www.gnu.org/copyleft/gpl.html.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * If you are developing and distributing open source applications under the
 * GPL License, then you are free to use Palo under the GPL License.  For OEMs,
 * ISVs, and VARs who distribute Palo with their products, and do not license
 * and distribute their source code under the GPL, Jedox provides a flexible
 * OEM Commercial License.
 *
 * \author Frank Celler, triagens GmbH, Cologne, Germany
 * \author Achim Brandt, triagens GmbH, Cologne, Germany
 * \author Jiri Junek, qBicon s.r.o., Prague, Czech Republic
 * 
 *
 */

#ifndef PALO_JOBS_RULE_CREATE_JOB_H
#define PALO_JOBS_RULE_CREATE_JOB_H 1

#include "palo.h"

#include "Olap/Rule.h"
#include "PaloDispatcher/DirectPaloJob.h"
#include "Parser/RuleParserDriver.h"

namespace palo {

////////////////////////////////////////////////////////////////////////////////
/// @brief rule create
////////////////////////////////////////////////////////////////////////////////

class SERVER_CLASS RuleCreateJob : public DirectPaloJob {
public:

	////////////////////////////////////////////////////////////////////////////////
	/// @brief factory method
	////////////////////////////////////////////////////////////////////////////////

	static PaloJob* create(PaloJobRequest* jobRequest) {
		return new RuleCreateJob(jobRequest);
	}

public:

	////////////////////////////////////////////////////////////////////////////////
	/// @brief constructor
	////////////////////////////////////////////////////////////////////////////////

	RuleCreateJob(PaloJobRequest* jobRequest) :
		DirectPaloJob(jobRequest) {
	}

	////////////////////////////////////////////////////////////////////////////////
	/// @brief gets job type
	////////////////////////////////////////////////////////////////////////////////

	JobType getType() {
		return WRITE_JOB;
	}

	////////////////////////////////////////////////////////////////////////////////
	/// @brief start working
	////////////////////////////////////////////////////////////////////////////////

	void compute() {
		bool ret = false;
		for (int commitTry = 0; commitTry < Commitable::COMMIT_REPEATS; commitTry++) {
			server = Context::getContext()->getServerCopy();
			findDatabase(true, true);
			findCube(true, true);

			string definition = "";
			if (jobRequest->definition) {
				definition = *(jobRequest->definition);
			}

			PRuleNode r;
			double inTextPosition = 0;

			if (jobRequest->activate != INACTIVE) {
				RuleParserDriver driver;
				driver.parse(definition);
				r = PRuleNode(driver.getResult());

				if (r) {
					// validate parse tree
					string errorMsg;
					bool ok = r->validate(server, database, cube, errorMsg);

					if (!ok) {
						throw ParameterException(ErrorException::ERROR_PARSING_RULE, errorMsg, PaloRequestHandler::DEFINITION, definition);
					}
				} else {
					// got no parse tree
					throw ParameterException(ErrorException::ERROR_PARSING_RULE, driver.getErrorMessage(), PaloRequestHandler::DEFINITION, definition);
				}
				inTextPosition = driver.getPosition();
			}

			string externalIdentifier = "";
			if (jobRequest->externalIdentifier) {
				externalIdentifier = *(jobRequest->externalIdentifier);
			}
			string comment = "";
			if (jobRequest->comment) {
				comment = *(jobRequest->comment);
			}

			double position = 0;
			if (jobRequest->dPositions && jobRequest->dPositions->size()) {
				position = jobRequest->dPositions->at(0);
			}
			if (inTextPosition) {
				position = inTextPosition;
			}
			PRule rule = cube->createRule(server, database, r, definition, externalIdentifier, comment, jobRequest->activate, user, true, 0, position);
			rules.push_back(rule);

			ret = server->commit();

			if (ret) {
				break;
			}
			clear();
		}
		if (!ret) {
			throw CommitException(ErrorException::ERROR_COMMIT_CANTCOMMIT, "Can't create new rule.");
		}
		server->invalidateCache();
		generateRuleResponse(cube, rules[0], jobRequest->useIdentifier);
	}
};

}

#endif
