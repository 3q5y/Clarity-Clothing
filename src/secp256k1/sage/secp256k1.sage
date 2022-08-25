# Test libsecp256k1' group operation implementations using prover.sage

import sys

load("group_prover.sage")
load("weierstrass_prover.sage")

def formula_secp256k1_gej_double_var(a):
  """libsecp256k1's secp256k1_gej_double_var, used by various addition functions"""
  rz = a.Z * a.Y
  rz = rz * 2
  t1 = a.X^2
  t1 = t1 * 3
  t2 = t1^2
  t3 = a.Y^2
  t3 = t3 * 2
  t4 = t3^2
  t4 = t4 * 2
  t3 = t3 * a.X
  rx = t3
  rx = rx * 4
  rx = -rx
  rx = rx + t2
  t2 = -t2
  t3 = t3 * 6
  t3 = t3 + t2
  ry = t1 * t3
  t2 = -t4
  ry = ry + t2
  return jacobianpoint(rx, ry, rz)

def formula_secp256k1_gej_add_var(branch, a, b):
  """libsecp256k1's secp256k1_gej_add_var"""
  if branch == 0:
    return (constraints(), constraints(nonzero={a.Infinity : 'a_infinite'}), b)
  if branch == 1:
    return (constraints(), constraints(zero={a.Infinity : 'a_finite'}, nonzero={b.Infinity : 'b_infinite'}), a)
  z22 = b.Z^2
  z12 = a.Z^2
  u1 = a.X * z22
  u2 = b.X * z12
  s1 = a.Y * z22
  s1 = s1 * b.Z
  s2 = b.Y * z12
  s2 = s2 * a.Z
  h = -u1
  h = h + u2
  i = -s1
  i = i + s2
  if branch == 2:
    r = formula_secp256k1_gej_double_var(a)
    return (constraints(), constraints(zero={h : 'h=0', i : 'i=0', a.Infinity : 'a_finite', b.Infinity : 'b_finite'}), r)
  if branch == 3:
    return (constraints(), constraints(zero={h : 'h=0', a.Infinity : 'a_finite', b.Infinity : 'b_finite'}, nonzero={i : 'i!=0'}), point_at_infinity())
  i2 = i^2
  h2 = h^2
  h3 = h2 * h
  h = h * b.Z
  rz = a.Z * h
  t = u1 * h2
  rx = t
  rx = rx * 2
  r