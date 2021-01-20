/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2021 Raden Solutions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
package org.netxms.reporting.services;

import java.util.List;
import java.util.UUID;
import org.hibernate.Session;
import org.hibernate.Transaction;
import org.hibernate.query.Query;
import org.netxms.reporting.Server;
import org.netxms.reporting.model.ReportResult;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Report result manager
 */
public class ReportResultManager
{
   public static final Logger logger = LoggerFactory.getLogger(ReportResultManager.class);

   private Server server;

   /**
    * Create new report result manager.
    *
    * @param server owning server
    */
   public ReportResultManager(Server server)
   {
      this.server = server;
   }

   /**
    * Save report result
    *
    * @param result report result to save
    */
   public void saveResult(ReportResult result)
   {
      Session session = server.getSessionFactory().getCurrentSession();
      Transaction txn = session.beginTransaction();
      try
      {
         session.save(result);
         txn.commit();
      }
      catch(Exception e)
      {
         txn.rollback();
         throw e;
      }
   }

   /**
    * List results for given report and user.
    *
    * @param reportId report ID
    * @param userId user ID
    * @return list of available results
    */
   public List<ReportResult> listResults(UUID reportId, int userId)
   {
      Session session = server.getSessionFactory().getCurrentSession();
      Transaction txn = session.beginTransaction();
      try
      {
         final Query<ReportResult> query =
               session.createQuery("FROM ReportResult result WHERE result.reportId = :reportId AND userId = :userId ORDER BY executionTime DESC", ReportResult.class)
               .setParameter("reportId", reportId)
               .setParameter("userId", userId);
         return query.getResultList();
      }
      finally
      {
         txn.commit();
      }
   }

   /**
    * Find specific report result by job ID.
    *
    * @param jobId job ID
    * @return list of matching results
    */
   public List<ReportResult> findReportsResult(UUID jobId)
   {
      Session session = server.getSessionFactory().getCurrentSession();
      Transaction txn = session.beginTransaction();
      try
      {
         final Query<ReportResult> query = session.createQuery("FROM ReportResult WHERE jobId = :jobId", ReportResult.class).setParameter("jobId", jobId);
         return query.getResultList();
      }
      finally
      {
         txn.commit();
      }
   }

   /**
    * Find report ID by job ID.
    *
    * @param jobId job ID
    * @return report ID or null
    */
   public UUID findReportId(UUID jobId)
   {
      UUID result = null;
      List<ReportResult> reportResult = findReportsResult(jobId);
      if (reportResult.size() > 0)
      {
         result = reportResult.get(0).getReportId();
      }
      return result;
   }

   /**
    * Delete report result.
    *
    * @param jobId job ID
    */
   public void deleteReportResult(UUID jobId)
   {
      final List<ReportResult> list = findReportsResult(jobId);
      final Session session = server.getSessionFactory().getCurrentSession();
      final Transaction txn = session.beginTransaction();
      try
      {
         for(ReportResult reportResult : list)
         {
            session.delete(reportResult);
         }
         txn.commit();
      }
      catch(Exception e)
      {
         txn.rollback();
         throw e;
      }
   }
}
