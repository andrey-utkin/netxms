/**
 * NetXMS - open source network management system
 * Copyright (C) 2016-2023 Raden Solutions
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
package org.netxms.nxmc.modules.objects.views.helpers;

import org.eclipse.jface.viewers.Viewer;
import org.eclipse.jface.viewers.ViewerFilter;
import org.netxms.client.NXCSession;
import org.netxms.client.topology.FdbEntry;
import org.netxms.nxmc.Registry;
import org.netxms.nxmc.base.views.AbstractViewerFilter;
import org.netxms.nxmc.localization.LocalizationHelper;

/**
 * Filter for switch forwarding database  
 */
public class FDBFilter extends ViewerFilter implements AbstractViewerFilter
{
   private static final String TYPE_MATCH_DYNAMIC = LocalizationHelper.getI18n(FDBFilter.class).tr("Dynamic").toLowerCase();
   private static final String TYPE_MATCH_STATIC = LocalizationHelper.getI18n(FDBFilter.class).tr("Static").toLowerCase();
   private static final String TYPE_MATCH_UNKNOWN = LocalizationHelper.getI18n(FDBFilter.class).tr("Unknown").toLowerCase();

   private NXCSession session = Registry.getSession();
   private String filterString = null;

   /**
    * @see org.eclipse.jface.viewers.ViewerFilter#select(org.eclipse.jface.viewers.Viewer, java.lang.Object, java.lang.Object)
    */
   @Override
   public boolean select(Viewer viewer, Object parentElement, Object element)
   {
      if ((filterString == null) || (filterString.isEmpty()))
         return true;

      final FdbEntry e = (FdbEntry)element;
      if (e.getAddress().toString().toLowerCase().contains(filterString))
         return true;
      if (Integer.toString(e.getPort()).contains(filterString))
         return true;
      if (e.getInterfaceName().toLowerCase().contains(filterString))
         return true;
      if (Integer.toString(e.getVlanId()).contains(filterString))
         return true;
      if ((e.getNodeId() != 0) && session.getObjectName(e.getNodeId()).toLowerCase().contains(filterString))
         return true;
      if (matchType(e))
         return true;

      String vendor = session.getVendorByMac(e.getAddress(), null);
      return (vendor != null) && vendor.toLowerCase().contains(filterString);
   }

   /**
    * Checks if filterString contains FDB type
    */
   private boolean matchType(FdbEntry en)
   {
      switch(en.getType())
      {
         case 3:
            return TYPE_MATCH_DYNAMIC.contains(filterString);
         case 5:
            return TYPE_MATCH_STATIC.contains(filterString);
         default:
            return TYPE_MATCH_UNKNOWN.contains(filterString);
      }
   }

   /**
    * @see org.netxms.nxmc.base.views.AbstractViewerFilter#setFilterString(java.lang.String)
    */
   @Override
   public void setFilterString(String filterString)
   {
      this.filterString = filterString.toLowerCase();
   }   
}
