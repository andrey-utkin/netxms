/**
 * NetXMS - open source network management system
 * Copyright (C) 2003-2022 Victor Kirhenshtein
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
package org.netxms.nxmc.modules.dashboards.widgets;

import org.eclipse.swt.SWT;
import org.eclipse.swt.events.FocusAdapter;
import org.eclipse.swt.events.FocusEvent;
import org.netxms.client.dashboards.DashboardElement;
import org.netxms.nxmc.modules.alarms.widgets.AlarmList;
import org.netxms.nxmc.modules.dashboards.config.AlarmViewerConfig;
import org.netxms.nxmc.modules.dashboards.views.AbstractDashboardView;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * Alarm viewer element for dashboard
 */
public class AlarmViewerElement extends ElementWidget
{
   private static final Logger logger = LoggerFactory.getLogger(AlarmViewerElement.class);

	private AlarmList viewer;
	private AlarmViewerConfig config;

	/**
	 * Create new alarm viewer element
	 * 
	 * @param parent Dashboard control
	 * @param element Dashboard element
	 * @param viewPart viewPart
	 */
   public AlarmViewerElement(DashboardControl parent, DashboardElement element, AbstractDashboardView view)
	{
      super(parent, element, view);

		try
		{
			config = AlarmViewerConfig.createFromXml(element.getData());
		}
		catch(Exception e)
		{
         logger.error("Cannot parse dashboard element configuration", e);
			config = new AlarmViewerConfig();
		}

      processCommonSettings(config);

      viewer = new AlarmList(view, getContentArea(), SWT.NONE, "Dashboard.AlarmList", null);
      viewer.setRootObject(getEffectiveObjectId(config.getObjectId()));
      viewer.setSeverityFilter(config.getSeverityFilter());
      viewer.setStateFilter(config.getStateFilter());
      viewer.setIsLocalSoundEnabled(config.getIsLocalSoundEnabled());
      viewer.getViewer().getControl().addFocusListener(new FocusAdapter() {
         @Override
         public void focusGained(FocusEvent e)
         {
            setSelectionProviderDelegate(viewer.getSelectionProvider());
         }
      });
	}
}
