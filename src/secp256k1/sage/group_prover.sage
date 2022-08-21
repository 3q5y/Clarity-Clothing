# This code supports verifying group implementations which have branches
# or conditional statements (like cmovs), by allowing each execution path
# to independently set assumptions on input or intermediary variables.
#
# The general approach is:
# * A constraint is a tuple of two sets of symbolic expressions:
#   the first of which are required to evaluate to zero, the second of which
#   are required to evaluate to nonzero.
#   - A constraint is said to be conflicting if any of its nonzero expressions
#     is in the ideal with basis the zero expressions (in other words: when the
#     zero expressions imply that one of the nonzero expressions are zero).
# * There is a list of laws that describe the intended behaviour, including
#   laws for addition and doubling. Each law is called with the symbolic point
#   coordinates as arguments, and returns:
#   - A constraint describing the assumptions under which it is applicable,
#     called "assumeLaw"
#   - A constraint describing the requirements of the law, called "require"
# * Implementations are transliterated into functions that operate as well on
#   algebraic input points, and are called once per combination of branches
#   executed. Each execution returns:
#   - A constraint describing the assumptions this implementation requires
#     (such as Z1=1), called "assumeFormula"
#   - A constraint describing the assumptions this specific branch requires,
#     but which is by construction guaranteed to cover the entire space by
#     merging the results from all branches, called "assumeBranch"
#   - The result of the computation
# * All combinations of laws with implementation branches are tried, and:
#   - If the combination of assumeLaw, assumeFormula, and assumeBranch results
#     in a conflict, it means this law does not apply to this branch, and it is
#     skipped.
#   - For others, we try to prove the require constraints hold, assuming the
#     information in assumeLaw + assumeFormula + assumeBranch, and if this does
#     not succeed, we fail.
#     + To prove an expression is zero, we check whether it belongs to the
#       ideal with the assumed zero expressions as basis. This test is exact.
#     + To prove an expression is nonzero, we check whether each of its
#       factors is contained in the set of nonzero assumptions' factors.
#       This test is not exact, so various combinations of original and
#       reduced expressions' factors are tried.
#   - If we succeed, we print out the assumptions from assumeFormula that
#     weren't implied by assumeLaw already. Those from assumeBranch are skipped,
#     as we assume that all constraints in it are complementary with each other.
#
# Based on the sage verification scripts used in the Explicit-Formulas Database
# by Tanja Lange and others, see http://hyperelliptic.org/EFD

class fastfrac:
  """Fractions over rings."""

  def __init__(self,R,top,bot=1):
    """Construct a fractional, given a ring, a numerator, and denominator."""
    self.R = R
    if parent(top) == ZZ or parent(top) == R:
      self.top = R(top)
      self.bot = R(bot)
    elif top.__class__ == fastfrac:
      self.top = top.top
      self.bot = top.bot * bot
    else:
      self.top = R(numerator(top))
      self.bot = R(denominator(top)) * bot

  def iszero(self,I):
    """Return whether this f