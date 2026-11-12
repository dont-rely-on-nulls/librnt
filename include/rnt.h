/* rnt.h: a subsystem's side of RNT's native protocol
  Copyright 2026 Marcos Magueta <maguetamarcos@gmail.com>
  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#ifndef RNT_H
#define RNT_H

typedef struct rnt_ctx rnt_ctx;

typedef struct {
} rnt;

enum rnt_protocol {
  RNT_PROTOCOL_DIRECTORY = 1,
  RNT_PROTOCOL_RELATION,
  RNT_PROTOCOL_REGISTRY
};

enum rnt_status { RNT_STATUS_OK = 1 };

typedef struct {
} rnt_attribute;

typedef struct {
} rnt_tuple;

typedef struct {
} rnt_multigroup;

typedef struct {
} rnt_relation;

enum rnt_object {
  RNT_OBJECT_BRANCH = 1,
  RNT_OBJECT_NAMESPACE,
  RNT_OBJECT_MULTIGROUP,
  RNT_OBJECT_RELATION
};

typedef void (*rnt_body)(const rnt_ctx *ctx, void *data);

void rnt_with_branch(rnt *c, const char *branch, rnt_body body, void *data);
void rnt_with_multigroup(const rnt_ctx *parent, const char *multigroup,
                         rnt_body body, void *data);
void rnt_with_relation(const rnt_ctx *parent, const char *schema,
                       const char *relation, const rnt_attribute *heading,
                       rnt_body body, void *data);

void rnt_execute_protocol(const rnt_ctx *ctx, const enum rnt_object object_type,
                          const enum rnt_protocol protocol, const void *object);

void rnt_instantiate_multigroup(const rnt_ctx *ctx, const char *name,
                                const rnt_tuple *tuples);
void rnt_instantiate_relation(const rnt_ctx *ctx, const char *name,
                              const rnt_attribute *heading);

#endif
