//
// Generated by JTB 1.3.2
//

package org.gnunet.seaspider.parser.nodes;

/**
 * Grammar production:
 * <PRE>
 * f0 -> ANDExpression()
 * f1 -> [ "^" ExclusiveORExpression() ]
 * </PRE>
 */
public class ExclusiveORExpression implements Node {
   public ANDExpression f0;
   public NodeOptional f1;

   public ExclusiveORExpression(ANDExpression n0, NodeOptional n1) {
      f0 = n0;
      f1 = n1;
   }

   public void accept(org.gnunet.seaspider.parser.visitors.Visitor v) {
      v.visit(this);
   }
   public <R,A> R accept(org.gnunet.seaspider.parser.visitors.GJVisitor<R,A> v, A argu) {
      return v.visit(this,argu);
   }
   public <R> R accept(org.gnunet.seaspider.parser.visitors.GJNoArguVisitor<R> v) {
      return v.visit(this);
   }
   public <A> void accept(org.gnunet.seaspider.parser.visitors.GJVoidVisitor<A> v, A argu) {
      v.visit(this,argu);
   }
}

