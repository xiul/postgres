/*-------------------------------------------------------------------------
 *
 * alter.h
 *	  prototypes for commands/alter.c
 *
 *
 * Portions Copyright (c) 1996-2011, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 * src/include/commands/alter.h
 *
 *-------------------------------------------------------------------------
 */
#ifndef ALTER_H
#define ALTER_H

#include "nodes/parsenodes.h"
#include "utils/acl.h"
#include "utils/relcache.h"

extern void ExecRenameStmt(RenameStmt *stmt);
extern void ExecAlterObjectSchemaStmt(AlterObjectSchemaStmt *stmt);
extern void AlterObjectNamespace(Relation rel, int cacheId,
								 Oid classId, Oid objid, Oid nspId,
								 int Anum_name, int Anum_namespace, int Anum_owner,
								 AclObjectKind acl_kind,
								 bool superuser_only);
extern void ExecAlterOwnerStmt(AlterOwnerStmt *stmt);

#endif   /* ALTER_H */
